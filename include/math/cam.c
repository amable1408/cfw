/*
 * cam.c - Camera projection and view matrices for the CFW math module.
 *
 * See cam.h for API documentation and usage examples.
 */

#include <math/cam.h>

/*==============================================================================
 * MARK: - Cam API
 *============================================================================*/

void math_cam_frustum_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_rh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_frustum_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_frustum_rh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cr = DEFAULT_INITIALIZATION;
    vec3 cu = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, ce);
    _math_vec3_raw_to_cglm(dir, cr);
    _math_vec3_raw_to_cglm(up, cu);
    glmc_look_rh_no(ce, cr, cu, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_2(Vec3 const eye, Vec3 const dir, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cr = DEFAULT_INITIALIZATION;
    vec3 cu = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, ce);
    _math_vec3_to_cglm(dir, cr);
    _math_vec3_to_cglm(up, cu);
    glmc_look_rh_no(ce, cr, cu, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_look_anyup_1(FSize const *const eye, FSize const *const dir, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cr = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, ce);
    _math_vec3_raw_to_cglm(dir, cr);
    glmc_look_anyup_rh_no(ce, cr, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_look_anyup_2(Vec3 const eye, Vec3 const dir) {
    trace_log_push(LOG_METADATA);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cr = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, ce);
    _math_vec3_to_cglm(dir, cr);
    glmc_look_anyup_rh_no(ce, cr, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_lookat_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "center", (void*) center);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cc = DEFAULT_INITIALIZATION;
    vec3 cu = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, ce);
    _math_vec3_raw_to_cglm(center, cc);
    _math_vec3_raw_to_cglm(up, cu);
    glmc_lookat_rh_no(ce, cc, cu, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_lookat_2(Vec3 const eye, Vec3 const center, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 ce = DEFAULT_INITIALIZATION;
    vec3 cc = DEFAULT_INITIALIZATION;
    vec3 cu = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, ce);
    _math_vec3_to_cglm(center, cc);
    _math_vec3_to_cglm(up, cu);
    glmc_lookat_rh_no(ce, cc, cu, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_rh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_rh_no((float) left, (float) right, (float) bottom, (float) top, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_1(FSize const *const box, FSize *const dest) {
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

Mat4 math_cam_ortho_aabb_2(Box const box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_rh_no(cbox, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_p_1(FSize const *const box, FSize const padding, FSize *const dest) {
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

Mat4 math_cam_ortho_aabb_p_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_p_rh_no(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_aabb_pz_1(FSize const *const box, FSize const padding, FSize *const dest) {
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

Mat4 math_cam_ortho_aabb_pz_2(Box const box, FSize const padding) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_ortho_aabb_pz_rh_no(cbox, (float) padding, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_1(FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_rh_no((float) aspect, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_2(FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_rh_no((float) aspect, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_ortho_default_s_1(FSize const aspect, FSize const size, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_rh_no((float) aspect, (float) size, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_ortho_default_s_2(FSize const aspect, FSize const size) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_ortho_default_s_rh_no((float) aspect, (float) size, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_rh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_aspect_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_aspect_rh_no(cp);

    trace_log_pop();

    return result;
}

void math_cam_persp_decomp_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cnear = DEFAULT_INITIALIZATION;
    float cfar = DEFAULT_INITIALIZATION;
    float ctop = DEFAULT_INITIALIZATION;
    float cbottom = DEFAULT_INITIALIZATION;
    float cleft = DEFAULT_INITIALIZATION;
    float cright = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_rh_no(cp, &cnear, &cfar, &ctop, &cbottom, &cleft, &cright);

    *near_z = (FSize) cnear;
    *far_z = (FSize) cfar;
    *top = (FSize) ctop;
    *bottom = (FSize) cbottom;
    *left = (FSize) cleft;
    *right = (FSize) cright;

    trace_log_pop();
}

FSize math_cam_persp_decomp_far_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cfar = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_far_rh_no(cp, &cfar);

    FSize const result = (FSize) cfar;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_far_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cfar = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_far_rh_no(cp, &cfar);

    FSize const result = (FSize) cfar;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_near_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cnear = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_near_rh_no(cp, &cnear);

    FSize const result = (FSize) cnear;

    trace_log_pop();

    return result;
}

FSize math_cam_persp_decomp_near_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cnear = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_decomp_near_rh_no(cp, &cnear);

    FSize const result = (FSize) cnear;

    trace_log_pop();

    return result;
}

void math_cam_persp_decomp_x_1(FSize const *const proj, FSize *const left, FSize *const right) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "left", (void*) left);
    error_check_null(LOG_METADATA, "right", (void*) right);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cleft = DEFAULT_INITIALIZATION;
    float cright = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_x_rh_no(cp, &cleft, &cright);

    *left = (FSize) cleft;
    *right = (FSize) cright;

    trace_log_pop();
}

void math_cam_persp_decomp_y_1(FSize const *const proj, FSize *const top, FSize *const bottom) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "top", (void*) top);
    error_check_null(LOG_METADATA, "bottom", (void*) bottom);

    mat4 cp = DEFAULT_INITIALIZATION;
    float ctop = DEFAULT_INITIALIZATION;
    float cbottom = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_y_rh_no(cp, &ctop, &cbottom);

    *top = (FSize) ctop;
    *bottom = (FSize) cbottom;

    trace_log_pop();
}

void math_cam_persp_decomp_z_1(FSize const *const proj, FSize *const near_z, FSize *const far_z) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "near_z", (void*) near_z);
    error_check_null(LOG_METADATA, "far_z", (void*) far_z);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cnear = DEFAULT_INITIALIZATION;
    float cfar = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decomp_z_rh_no(cp, &cnear, &cfar);

    *near_z = (FSize) cnear;
    *far_z = (FSize) cfar;

    trace_log_pop();
}

void math_cam_persp_decompv_1(FSize const *const proj, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;
    float cdest[6] = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_decompv_rh_no(cp, cdest);

    for (USize i = 0; i < 6; i += 1) {
        dest[i] = (FSize) cdest[i];
    }

    trace_log_pop();
}

FSize math_cam_persp_fovy_1(FSize const *const proj) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_rh_no(cp);

    trace_log_pop();

    return result;
}

FSize math_cam_persp_fovy_2(Mat4 const proj) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);

    FSize const result = (FSize) glmc_persp_fovy_rh_no(cp);

    trace_log_pop();

    return result;
}

void math_cam_persp_move_far_1(FSize const *const proj, FSize const delta_far, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    glmc_persp_move_far_rh_no(cp, (float) delta_far);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_persp_move_far_2(Mat4 const proj, FSize const delta_far) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_move_far_rh_no(cp, (float) delta_far);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}

void math_cam_persp_sizes_1(FSize const *const proj, FSize const fovy, FSize *const dest) {
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

Vec4 math_cam_persp_sizes_2(Mat4 const proj, FSize const fovy) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    glmc_persp_sizes_rh_no(cp, (float) fovy, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_perspective_1(FSize const fovy, FSize const aspect, FSize const near_z, FSize const far_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_rh_no((float) fovy, (float) aspect, (float) near_z, (float) far_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_2(FSize const fovy, FSize const aspect, FSize const near_z, FSize const far_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_perspective_rh_no((float) fovy, (float) aspect, (float) near_z, (float) far_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_perspective_default_1(FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    _math_cglm_perspective_default_rh_no((float) aspect, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_default_2(FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    _math_cglm_perspective_default_rh_no((float) aspect, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_perspective_default_infinite_1(FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    _math_cglm_perspective_default_infinite_rh_no((float) aspect, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_default_infinite_2(FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    _math_cglm_perspective_default_infinite_rh_no((float) aspect, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_perspective_infinite_1(FSize const fovy, FSize const aspect, FSize const near_z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    _math_cglm_perspective_infinite_rh_no((float) fovy, (float) aspect, (float) near_z, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_infinite_2(FSize const fovy, FSize const aspect, FSize const near_z) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    _math_cglm_perspective_infinite_rh_no((float) fovy, (float) aspect, (float) near_z, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_cam_perspective_resize_1(FSize const *const proj, FSize const aspect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "proj", (void*) proj);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);
    _math_mat4_raw_from_cglm(cp, dest);

    trace_log_pop();
}

Mat4 math_cam_perspective_resize_2(Mat4 const proj, FSize const aspect) {
    trace_log_push(LOG_METADATA);

    mat4 cp = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(proj, cp);
    _math_cglm_perspective_resize((float) aspect, cp);

    Mat4 const result = _math_mat4_from_cglm(cp);

    trace_log_pop();

    return result;
}