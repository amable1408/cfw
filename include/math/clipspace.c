/*
 * clipspace.c - Clipspace projection/view variants for the CFW math module.
 *
 * See clipspace.h for API documentation and usage examples.
 */

#include <math/clipspace.h>

/*==============================================================================
 * MARK: - Clipspace camera API
 *============================================================================*/

void math_cam_frustum_lh_no_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_lh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_frustum_lh_no_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_lh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_frustum_lh_zo_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_lh_zo((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_frustum_lh_zo_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_lh_zo((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_frustum_rh_no_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_rh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_frustum_rh_no_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_rh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_frustum_rh_zo_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_rh_zo((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_frustum_rh_zo_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_rh_zo((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_anyup_lh_no_1(FSize const *const eye, FSize const *const dir, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(dir, c_dir);
    glmc_look_anyup_lh_no(c_eye, c_dir, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_anyup_lh_no_2(Vec3 const eye, Vec3 const dir) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(dir, c_dir);
    glmc_look_anyup_lh_no(c_eye, c_dir, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_anyup_lh_zo_1(FSize const *const eye, FSize const *const dir, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(dir, c_dir);
    glmc_look_anyup_lh_zo(c_eye, c_dir, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_anyup_lh_zo_2(Vec3 const eye, Vec3 const dir) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(dir, c_dir);
    glmc_look_anyup_lh_zo(c_eye, c_dir, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_anyup_rh_no_1(FSize const *const eye, FSize const *const dir, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(dir, c_dir);
    glmc_look_anyup_rh_no(c_eye, c_dir, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_anyup_rh_no_2(Vec3 const eye, Vec3 const dir) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(dir, c_dir);
    glmc_look_anyup_rh_no(c_eye, c_dir, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_anyup_rh_zo_1(FSize const *const eye, FSize const *const dir, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(dir, c_dir);
    glmc_look_anyup_rh_zo(c_eye, c_dir, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_anyup_rh_zo_2(Vec3 const eye, Vec3 const dir) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(dir, c_dir);
    glmc_look_anyup_rh_zo(c_eye, c_dir, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_lh_no_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(dir, c_dir);
    _math_vec3_raw_to_cglm(up, c_up);
    glmc_look_lh_no(c_eye, c_dir, c_up, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_lh_no_2(Vec3 const eye, Vec3 const dir, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(dir, c_dir);
    _math_vec3_to_cglm(up, c_up);
    glmc_look_lh_no(c_eye, c_dir, c_up, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_lh_zo_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(dir, c_dir);
    _math_vec3_raw_to_cglm(up, c_up);
    glmc_look_lh_zo(c_eye, c_dir, c_up, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_lh_zo_2(Vec3 const eye, Vec3 const dir, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(dir, c_dir);
    _math_vec3_to_cglm(up, c_up);
    glmc_look_lh_zo(c_eye, c_dir, c_up, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_rh_no_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(dir, c_dir);
    _math_vec3_raw_to_cglm(up, c_up);
    glmc_look_rh_no(c_eye, c_dir, c_up, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_rh_no_2(Vec3 const eye, Vec3 const dir, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(dir, c_dir);
    _math_vec3_to_cglm(up, c_up);
    glmc_look_rh_no(c_eye, c_dir, c_up, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_rh_zo_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(dir, c_dir);
    _math_vec3_raw_to_cglm(up, c_up);
    glmc_look_rh_zo(c_eye, c_dir, c_up, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_rh_zo_2(Vec3 const eye, Vec3 const dir, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_dir = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(dir, c_dir);
    _math_vec3_to_cglm(up, c_up);
    glmc_look_rh_zo(c_eye, c_dir, c_up, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_lookat_lh_no_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "center", (void*) center);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_center = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(center, c_center);
    _math_vec3_raw_to_cglm(up, c_up);
    glmc_lookat_lh_no(c_eye, c_center, c_up, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_lookat_lh_no_2(Vec3 const eye, Vec3 const center, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_center = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(center, c_center);
    _math_vec3_to_cglm(up, c_up);
    glmc_lookat_lh_no(c_eye, c_center, c_up, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_lookat_lh_zo_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "center", (void*) center);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_center = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(center, c_center);
    _math_vec3_raw_to_cglm(up, c_up);
    glmc_lookat_lh_zo(c_eye, c_center, c_up, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_lookat_lh_zo_2(Vec3 const eye, Vec3 const center, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_center = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(center, c_center);
    _math_vec3_to_cglm(up, c_up);
    glmc_lookat_lh_zo(c_eye, c_center, c_up, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_lookat_rh_no_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "center", (void*) center);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_center = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(center, c_center);
    _math_vec3_raw_to_cglm(up, c_up);
    glmc_lookat_rh_no(c_eye, c_center, c_up, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_lookat_rh_no_2(Vec3 const eye, Vec3 const center, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_center = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(center, c_center);
    _math_vec3_to_cglm(up, c_up);
    glmc_lookat_rh_no(c_eye, c_center, c_up, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_lookat_rh_zo_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "center", (void*) center);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_center = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, c_eye);
    _math_vec3_raw_to_cglm(center, c_center);
    _math_vec3_raw_to_cglm(up, c_up);
    glmc_lookat_rh_zo(c_eye, c_center, c_up, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_lookat_rh_zo_2(Vec3 const eye, Vec3 const center, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 c_eye = DEFAULT_INITIALIZATION;
    vec3 c_center = DEFAULT_INITIALIZATION;
    vec3 c_up = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, c_eye);
    _math_vec3_to_cglm(center, c_center);
    _math_vec3_to_cglm(up, c_up);
    glmc_lookat_rh_zo(c_eye, c_center, c_up, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_lh_no_1(FSize const *const box, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_lh_no(cbox, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_lh_no_2(Box const box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_lh_no(cbox, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_lh_zo_1(FSize const *const box, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_lh_zo(cbox, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_lh_zo_2(Box const box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_lh_zo(cbox, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_p_lh_no_1(FSize const *const box, FSize const padding, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_p_lh_no(cbox, (float) padding, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_p_lh_no_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_p_lh_no(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_p_lh_zo_1(FSize const *const box, FSize const padding, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_p_lh_zo(cbox, (float) padding, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_p_lh_zo_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_p_lh_zo(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_p_rh_no_1(FSize const *const box, FSize const padding, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_p_rh_no(cbox, (float) padding, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_p_rh_no_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_p_rh_no(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_p_rh_zo_1(FSize const *const box, FSize const padding, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_p_rh_zo(cbox, (float) padding, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_p_rh_zo_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_p_rh_zo(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_pz_lh_no_1(FSize const *const box, FSize const padding, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_pz_lh_no(cbox, (float) padding, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_pz_lh_no_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_pz_lh_no(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_pz_lh_zo_1(FSize const *const box, FSize const padding, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_pz_lh_zo(cbox, (float) padding, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_pz_lh_zo_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_pz_lh_zo(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_pz_rh_no_1(FSize const *const box, FSize const padding, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_pz_rh_no(cbox, (float) padding, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_pz_rh_no_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_pz_rh_no(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_pz_rh_zo_1(FSize const *const box, FSize const padding, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_pz_rh_zo(cbox, (float) padding, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_pz_rh_zo_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_pz_rh_zo(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_rh_no_1(FSize const *const box, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_rh_no(cbox, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_rh_no_2(Box const box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_rh_no(cbox, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_rh_zo_1(FSize const *const box, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_ortho_aabb_rh_zo(cbox, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_aabb_rh_zo_2(Box const box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_rh_zo(cbox, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_lh_no_1(FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_lh_no((float) aspect, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_lh_no_2(FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_lh_no((float) aspect, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_lh_zo_1(FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_lh_zo((float) aspect, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_lh_zo_2(FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_lh_zo((float) aspect, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_rh_no_1(FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_rh_no((float) aspect, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_rh_no_2(FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_rh_no((float) aspect, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_rh_zo_1(FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_rh_zo((float) aspect, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_rh_zo_2(FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_rh_zo((float) aspect, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_s_lh_no_1(FSize const aspect, FSize const size, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_lh_no((float) aspect, (float) size, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_s_lh_no_2(FSize const aspect, FSize const size) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_lh_no((float) aspect, (float) size, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_s_lh_zo_1(FSize const aspect, FSize const size, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_lh_zo((float) aspect, (float) size, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_s_lh_zo_2(FSize const aspect, FSize const size) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_lh_zo((float) aspect, (float) size, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_s_rh_no_1(FSize const aspect, FSize const size, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_rh_no((float) aspect, (float) size, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_s_rh_no_2(FSize const aspect, FSize const size) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_rh_no((float) aspect, (float) size, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_s_rh_zo_1(FSize const aspect, FSize const size, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_rh_zo((float) aspect, (float) size, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_s_rh_zo_2(FSize const aspect, FSize const size) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_rh_zo((float) aspect, (float) size, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_lh_no_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_lh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_lh_no_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_lh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_lh_zo_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_lh_zo((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_lh_zo_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_lh_zo((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_rh_no_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_rh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_rh_no_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_rh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_rh_zo_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_rh_zo((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_rh_zo_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_rh_zo((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_lh_no_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_lh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_lh_no_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_lh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_lh_zo_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_lh_zo(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_lh_zo_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_lh_zo(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_rh_no_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_rh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_rh_no_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_rh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_rh_zo_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_rh_zo(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_rh_zo_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_rh_zo(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_far_lh_no_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_far_lh_no(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_far_lh_no_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_far_lh_no(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_far_lh_zo_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_far_lh_zo(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_far_lh_zo_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_far_lh_zo(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_far_rh_no_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_far_rh_no(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_far_rh_no_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_far_rh_no(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_far_rh_zo_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_far_rh_zo(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_far_rh_zo_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_far_rh_zo(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

void math_cam_persp_decomp_lh_no_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_near_z = DEFAULT_INITIALIZATION;
    float c_far_z = DEFAULT_INITIALIZATION;
    float c_top = DEFAULT_INITIALIZATION;
    float c_bottom = DEFAULT_INITIALIZATION;
    float c_left = DEFAULT_INITIALIZATION;
    float c_right = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_lh_no(cp, &c_near_z, &c_far_z, &c_top, &c_bottom, &c_left, &c_right);
    *near_z = (FSize) c_near_z;
    *far_z = (FSize) c_far_z;
    *top = (FSize) c_top;
    *bottom = (FSize) c_bottom;
    *left = (FSize) c_left;
    *right = (FSize) c_right;

    trace_log_pop();
}

void math_cam_persp_decomp_lh_zo_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_near_z = DEFAULT_INITIALIZATION;
    float c_far_z = DEFAULT_INITIALIZATION;
    float c_top = DEFAULT_INITIALIZATION;
    float c_bottom = DEFAULT_INITIALIZATION;
    float c_left = DEFAULT_INITIALIZATION;
    float c_right = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_lh_zo(cp, &c_near_z, &c_far_z, &c_top, &c_bottom, &c_left, &c_right);
    *near_z = (FSize) c_near_z;
    *far_z = (FSize) c_far_z;
    *top = (FSize) c_top;
    *bottom = (FSize) c_bottom;
    *left = (FSize) c_left;
    *right = (FSize) c_right;

    trace_log_pop();
}

FSize math_cam_persp_decomp_near_lh_no_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_near_lh_no(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_near_lh_no_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_near_lh_no(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_near_lh_zo_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_near_lh_zo(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_near_lh_zo_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_near_lh_zo(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_near_rh_no_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_near_rh_no(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_near_rh_no_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_near_rh_no(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_near_rh_zo_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_near_rh_zo(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_near_rh_zo_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_out = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_near_rh_zo(cp, &c_out);

    FSize const result = (FSize) c_out;

    trace_log_pop();

    return result;
}

void math_cam_persp_decomp_rh_no_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_near_z = DEFAULT_INITIALIZATION;
    float c_far_z = DEFAULT_INITIALIZATION;
    float c_top = DEFAULT_INITIALIZATION;
    float c_bottom = DEFAULT_INITIALIZATION;
    float c_left = DEFAULT_INITIALIZATION;
    float c_right = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_rh_no(cp, &c_near_z, &c_far_z, &c_top, &c_bottom, &c_left, &c_right);
    *near_z = (FSize) c_near_z;
    *far_z = (FSize) c_far_z;
    *top = (FSize) c_top;
    *bottom = (FSize) c_bottom;
    *left = (FSize) c_left;
    *right = (FSize) c_right;

    trace_log_pop();
}

void math_cam_persp_decomp_rh_zo_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_near_z = DEFAULT_INITIALIZATION;
    float c_far_z = DEFAULT_INITIALIZATION;
    float c_top = DEFAULT_INITIALIZATION;
    float c_bottom = DEFAULT_INITIALIZATION;
    float c_left = DEFAULT_INITIALIZATION;
    float c_right = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_rh_zo(cp, &c_near_z, &c_far_z, &c_top, &c_bottom, &c_left, &c_right);
    *near_z = (FSize) c_near_z;
    *far_z = (FSize) c_far_z;
    *top = (FSize) c_top;
    *bottom = (FSize) c_bottom;
    *left = (FSize) c_left;
    *right = (FSize) c_right;

    trace_log_pop();
}

void math_cam_persp_decomp_x_lh_no_1(FSize const *const proj, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_left = DEFAULT_INITIALIZATION;
    float c_right = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_x_lh_no(cp, &c_left, &c_right);
    *left = (FSize) c_left;
    *right = (FSize) c_right;

    trace_log_pop();
}

void math_cam_persp_decomp_x_lh_zo_1(FSize const *const proj, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_left = DEFAULT_INITIALIZATION;
    float c_right = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_x_lh_zo(cp, &c_left, &c_right);
    *left = (FSize) c_left;
    *right = (FSize) c_right;

    trace_log_pop();
}

void math_cam_persp_decomp_x_rh_no_1(FSize const *const proj, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_left = DEFAULT_INITIALIZATION;
    float c_right = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_x_rh_no(cp, &c_left, &c_right);
    *left = (FSize) c_left;
    *right = (FSize) c_right;

    trace_log_pop();
}

void math_cam_persp_decomp_x_rh_zo_1(FSize const *const proj, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_left = DEFAULT_INITIALIZATION;
    float c_right = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_x_rh_zo(cp, &c_left, &c_right);
    *left = (FSize) c_left;
    *right = (FSize) c_right;

    trace_log_pop();
}

void math_cam_persp_decomp_y_lh_no_1(FSize const *const proj, FSize *const top, FSize *const bottom) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_top = DEFAULT_INITIALIZATION;
    float c_bottom = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_y_lh_no(cp, &c_top, &c_bottom);
    *top = (FSize) c_top;
    *bottom = (FSize) c_bottom;

    trace_log_pop();
}

void math_cam_persp_decomp_y_lh_zo_1(FSize const *const proj, FSize *const top, FSize *const bottom) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_top = DEFAULT_INITIALIZATION;
    float c_bottom = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_y_lh_zo(cp, &c_top, &c_bottom);
    *top = (FSize) c_top;
    *bottom = (FSize) c_bottom;

    trace_log_pop();
}

void math_cam_persp_decomp_y_rh_no_1(FSize const *const proj, FSize *const top, FSize *const bottom) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_top = DEFAULT_INITIALIZATION;
    float c_bottom = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_y_rh_no(cp, &c_top, &c_bottom);
    *top = (FSize) c_top;
    *bottom = (FSize) c_bottom;

    trace_log_pop();
}

void math_cam_persp_decomp_y_rh_zo_1(FSize const *const proj, FSize *const top, FSize *const bottom) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_top = DEFAULT_INITIALIZATION;
    float c_bottom = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_y_rh_zo(cp, &c_top, &c_bottom);
    *top = (FSize) c_top;
    *bottom = (FSize) c_bottom;

    trace_log_pop();
}

void math_cam_persp_decomp_z_lh_no_1(FSize const *const proj, FSize *const near_z, FSize *const far_z) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_near_z = DEFAULT_INITIALIZATION;
    float c_far_z = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_z_lh_no(cp, &c_near_z, &c_far_z);
    *near_z = (FSize) c_near_z;
    *far_z = (FSize) c_far_z;

    trace_log_pop();
}

void math_cam_persp_decomp_z_lh_zo_1(FSize const *const proj, FSize *const near_z, FSize *const far_z) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_near_z = DEFAULT_INITIALIZATION;
    float c_far_z = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_z_lh_zo(cp, &c_near_z, &c_far_z);
    *near_z = (FSize) c_near_z;
    *far_z = (FSize) c_far_z;

    trace_log_pop();
}

void math_cam_persp_decomp_z_rh_no_1(FSize const *const proj, FSize *const near_z, FSize *const far_z) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_near_z = DEFAULT_INITIALIZATION;
    float c_far_z = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_z_rh_no(cp, &c_near_z, &c_far_z);
    *near_z = (FSize) c_near_z;
    *far_z = (FSize) c_far_z;

    trace_log_pop();
}

void math_cam_persp_decomp_z_rh_zo_1(FSize const *const proj, FSize *const near_z, FSize *const far_z) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);

    mat4 cp = DEFAULT_INITIALIZATION;
    float c_near_z = DEFAULT_INITIALIZATION;
    float c_far_z = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_z_rh_zo(cp, &c_near_z, &c_far_z);
    *near_z = (FSize) c_near_z;
    *far_z = (FSize) c_far_z;

    trace_log_pop();
}

void math_cam_persp_decompv_lh_no_1(FSize const *const proj, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cd[6] = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decompv_lh_no(cp, cd);

    for (USize i = 0; i < 6; i++) {
        dest[i] = (FSize) cd[i];
    }

    trace_log_pop();
}

void math_cam_persp_decompv_lh_zo_1(FSize const *const proj, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cd[6] = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decompv_lh_zo(cp, cd);

    for (USize i = 0; i < 6; i++) {
        dest[i] = (FSize) cd[i];
    }

    trace_log_pop();
}

void math_cam_persp_decompv_rh_no_1(FSize const *const proj, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cd[6] = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decompv_rh_no(cp, cd);

    for (USize i = 0; i < 6; i++) {
        dest[i] = (FSize) cd[i];
    }

    trace_log_pop();
}

void math_cam_persp_decompv_rh_zo_1(FSize const *const proj, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cd[6] = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decompv_rh_zo(cp, cd);

    for (USize i = 0; i < 6; i++) {
        dest[i] = (FSize) cd[i];
    }

    trace_log_pop();
}

FSize math_cam_persp_fovy_lh_no_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_lh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_fovy_lh_no_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_lh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_fovy_lh_zo_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_lh_zo(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_fovy_lh_zo_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_lh_zo(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_fovy_rh_no_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_rh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_fovy_rh_no_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_rh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_fovy_rh_zo_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_rh_zo(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_fovy_rh_zo_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_rh_zo(cp);

    trace_log_pop();

    return result;
}

void math_cam_persp_move_far_lh_no_1(FSize const *const proj, FSize const delta_far, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_move_far_lh_no(cp, (float) delta_far);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_persp_move_far_lh_no_2(Mat4 const proj, FSize const delta_far) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_move_far_lh_no(cp, (float) delta_far);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}

void math_cam_persp_move_far_lh_zo_1(FSize const *const proj, FSize const delta_far, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_move_far_lh_zo(cp, (float) delta_far);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_persp_move_far_lh_zo_2(Mat4 const proj, FSize const delta_far) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_move_far_lh_zo(cp, (float) delta_far);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}

void math_cam_persp_move_far_rh_no_1(FSize const *const proj, FSize const delta_far, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_move_far_rh_no(cp, (float) delta_far);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_persp_move_far_rh_no_2(Mat4 const proj, FSize const delta_far) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_move_far_rh_no(cp, (float) delta_far);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}

void math_cam_persp_move_far_rh_zo_1(FSize const *const proj, FSize const delta_far, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_move_far_rh_zo(cp, (float) delta_far);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_persp_move_far_rh_zo_2(Mat4 const proj, FSize const delta_far) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_move_far_rh_zo(cp, (float) delta_far);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}

void math_cam_persp_sizes_lh_no_1(FSize const *const proj, FSize const fovy, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_sizes_lh_no(cp, (float) fovy, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_cam_persp_sizes_lh_no_2(Mat4 const proj, FSize const fovy) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_sizes_lh_no(cp, (float) fovy, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_persp_sizes_lh_zo_1(FSize const *const proj, FSize const fovy, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_sizes_lh_zo(cp, (float) fovy, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_cam_persp_sizes_lh_zo_2(Mat4 const proj, FSize const fovy) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_sizes_lh_zo(cp, (float) fovy, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_persp_sizes_rh_no_1(FSize const *const proj, FSize const fovy, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_sizes_rh_no(cp, (float) fovy, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_cam_persp_sizes_rh_no_2(Mat4 const proj, FSize const fovy) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_sizes_rh_no(cp, (float) fovy, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_persp_sizes_rh_zo_1(FSize const *const proj, FSize const fovy, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_sizes_rh_zo(cp, (float) fovy, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_cam_persp_sizes_rh_zo_2(Mat4 const proj, FSize const fovy) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_sizes_rh_zo(cp, (float) fovy, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_perspective_lh_no_1(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_lh_no((float) fovy, (float) aspect, (float) near_val, (float) far_val, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_lh_no_2(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_lh_no((float) fovy, (float) aspect, (float) near_val, (float) far_val, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_perspective_lh_zo_1(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_lh_zo((float) fovy, (float) aspect, (float) near_val, (float) far_val, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_lh_zo_2(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_lh_zo((float) fovy, (float) aspect, (float) near_val, (float) far_val, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_perspective_resize_lh_no_1(FSize const *const proj, FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_resize_lh_no_2(Mat4 const proj, FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}

void math_cam_perspective_resize_lh_zo_1(FSize const *const proj, FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_resize_lh_zo_2(Mat4 const proj, FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}

void math_cam_perspective_resize_rh_no_1(FSize const *const proj, FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_resize_rh_no_2(Mat4 const proj, FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}

void math_cam_perspective_resize_rh_zo_1(FSize const *const proj, FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_resize_rh_zo_2(Mat4 const proj, FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}

void math_cam_perspective_rh_no_1(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_rh_no((float) fovy, (float) aspect, (float) near_val, (float) far_val, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_rh_no_2(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_rh_no((float) fovy, (float) aspect, (float) near_val, (float) far_val, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_perspective_rh_zo_1(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_rh_zo((float) fovy, (float) aspect, (float) near_val, (float) far_val, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_rh_zo_2(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_rh_zo((float) fovy, (float) aspect, (float) near_val, (float) far_val, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_project_no_1(FSize const *const pos, FSize const *const m, FSize const *const vp, FSize *const dest) {
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

Vec3 math_cam_project_no_2(Vec3 const pos, Mat4 const m, Vec4 const vp) {
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

FSize math_cam_project_z_no_1(FSize const *const pos, FSize const *const m) {
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

FSize math_cam_project_z_no_2(Vec3 const pos, Mat4 const m) {
    trace_log_push(LOG_METADATA);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pos, cpos);
    _math_mat4_to_cglm(m, cm);

    FSize const result = (FSize) glmc_project_z_no(cpos, cm);

    trace_log_pop();

    return result;
}

FSize math_cam_project_z_zo_1(FSize const *const pos, FSize const *const m) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pos", (void*) pos);
    error_check_null(LOG_METADATA, "m", (void*) m);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(pos, cpos);
    _math_mat4_raw_to_cglm(m, cm);

    FSize const result = (FSize) glmc_project_z_zo(cpos, cm);

    trace_log_pop();

    return result;
}

FSize math_cam_project_z_zo_2(Vec3 const pos, Mat4 const m) {
    trace_log_push(LOG_METADATA);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pos, cpos);
    _math_mat4_to_cglm(m, cm);

    FSize const result = (FSize) glmc_project_z_zo(cpos, cm);

    trace_log_pop();

    return result;
}

void math_cam_project_zo_1(FSize const *const pos, FSize const *const m, FSize const *const vp, FSize *const dest) {
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
    glmc_project_zo(cpos, cm, cvp, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_cam_project_zo_2(Vec3 const pos, Mat4 const m, Vec4 const vp) {
    trace_log_push(LOG_METADATA);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pos, cpos);
    _math_mat4_to_cglm(m, cm);
    _math_vec4_to_cglm(vp, cvp);
    glmc_project_zo(cpos, cm, cvp, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_unprojecti_no_1(FSize const *const pos, FSize const *const inv_mat, FSize const *const vp, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pos", (void*) pos);
    error_check_null(LOG_METADATA, "inv_mat", (void*) inv_mat);
    error_check_null(LOG_METADATA, "vp", (void*) vp);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(pos, cpos);
    _math_mat4_raw_to_cglm(inv_mat, cm);
    _math_vec4_raw_to_cglm(vp, cvp);
    glmc_unprojecti_no(cpos, cm, cvp, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_cam_unprojecti_no_2(Vec3 const pos, Mat4 const inv_mat, Vec4 const vp) {
    trace_log_push(LOG_METADATA);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pos, cpos);
    _math_mat4_to_cglm(inv_mat, cm);
    _math_vec4_to_cglm(vp, cvp);
    glmc_unprojecti_no(cpos, cm, cvp, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_unprojecti_zo_1(FSize const *const pos, FSize const *const inv_mat, FSize const *const vp, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pos", (void*) pos);
    error_check_null(LOG_METADATA, "inv_mat", (void*) inv_mat);
    error_check_null(LOG_METADATA, "vp", (void*) vp);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(pos, cpos);
    _math_mat4_raw_to_cglm(inv_mat, cm);
    _math_vec4_raw_to_cglm(vp, cvp);
    glmc_unprojecti_zo(cpos, cm, cvp, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_cam_unprojecti_zo_2(Vec3 const pos, Mat4 const inv_mat, Vec4 const vp) {
    trace_log_push(LOG_METADATA);

    vec3 cpos = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cvp = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pos, cpos);
    _math_mat4_to_cglm(inv_mat, cm);
    _math_vec4_to_cglm(vp, cvp);
    glmc_unprojecti_zo(cpos, cm, cvp, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}