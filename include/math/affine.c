/*
 * affine.c - Affine 4x4 transforms for the CFW math module.
 *
 * See affine.h for API documentation and usage examples.
 */

#include <math/affine.h>

/*==============================================================================
 * MARK: - Affine API
 *============================================================================*/

void math_affine_decompose_1(FSize const *const mat, FSize *const translation, FSize *const rotation, FSize *const scale) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "translation", (void*) translation);
    error_check_null(LOG_METADATA, "rotation", (void*) rotation);
    error_check_null(LOG_METADATA, "scale", (void*) scale);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 ct = DEFAULT_INITIALIZATION;
    mat4 cr = DEFAULT_INITIALIZATION;
    vec3 cs = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_decompose(cm, ct, cr, cs);
    _math_vec4_raw_from_cglm(ct, translation);
    _math_mat4_raw_from_cglm(cr, rotation);
    _math_vec3_raw_from_cglm(cs, scale);

    trace_log_pop();
}

void math_affine_decompose_rs_1(FSize const *const mat, FSize *const rotation, FSize *const scale) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "rotation", (void*) rotation);
    error_check_null(LOG_METADATA, "scale", (void*) scale);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cr = DEFAULT_INITIALIZATION;
    vec3 cs = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_decompose_rs(cm, cr, cs);
    _math_mat4_raw_from_cglm(cr, rotation);
    _math_vec3_raw_from_cglm(cs, scale);

    trace_log_pop();
}

void math_affine_decompose_scalev_1(FSize const *const mat, FSize *const scale) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "scale", (void*) scale);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cs = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_decompose_scalev(cm, cs);
    _math_vec3_raw_from_cglm(cs, scale);

    trace_log_pop();
}

Vec3 math_affine_decompose_scalev_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cs = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_decompose_scalev(cm, cs);

    Vec3 const result = _math_vec3_from_cglm(cs);

    trace_log_pop();

    return result;
}

void math_affine_inv_tr_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_inv_tr(cm);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_inv_tr_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_inv_tr(cm);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 ca = DEFAULT_INITIALIZATION;
    mat4 cb = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(a, ca);
    _math_mat4_raw_to_cglm(b, cb);
    glmc_mul(ca, cb, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_mul_2(Mat4 const a, Mat4 const b) {
    trace_log_push(LOG_METADATA);

    mat4 ca = DEFAULT_INITIALIZATION;
    mat4 cb = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(a, ca);
    _math_mat4_to_cglm(b, cb);
    glmc_mul(ca, cb, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_mul_rot_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 ca = DEFAULT_INITIALIZATION;
    mat4 cb = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(a, ca);
    _math_mat4_raw_to_cglm(b, cb);
    glmc_mul_rot(ca, cb, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_mul_rot_2(Mat4 const a, Mat4 const b) {
    trace_log_push(LOG_METADATA);

    mat4 ca = DEFAULT_INITIALIZATION;
    mat4 cb = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(a, ca);
    _math_mat4_to_cglm(b, cb);
    glmc_mul_rot(ca, cb, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_rotate_1(FSize const *const mat, FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    _math_vec3_raw_to_cglm(axis, cv);
    glmc_rotate(cm, (float) angle, cv);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_rotate_2(Mat4 const mat, FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    _math_vec3_to_cglm(axis, cv);
    glmc_rotate(cm, (float) angle, cv);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_rotate_at_1(FSize const *const mat, FSize const *const pivot, FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "pivot", (void*) pivot);
    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cp = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    _math_vec3_raw_to_cglm(pivot, cp);
    _math_vec3_raw_to_cglm(axis, cv);
    glmc_rotate_at(cm, cp, (float) angle, cv);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_rotate_at_2(Mat4 const mat, Vec3 const pivot, FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cp = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    _math_vec3_to_cglm(pivot, cp);
    _math_vec3_to_cglm(axis, cv);
    glmc_rotate_at(cm, cp, (float) angle, cv);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_rotate_atm_1(FSize const *const pivot, FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "pivot", (void*) pivot);
    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;
    vec3 cp = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(pivot, cp);
    _math_vec3_raw_to_cglm(axis, cv);
    glmc_rotate_atm(cd, cp, (float) angle, cv);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_rotate_atm_2(Vec3 const pivot, FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;
    vec3 cp = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(pivot, cp);
    _math_vec3_to_cglm(axis, cv);
    glmc_rotate_atm(cd, cp, (float) angle, cv);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_rotate_make_1(FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(axis, cv);
    glmc_rotate_make(cd, (float) angle, cv);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_rotate_make_2(FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(axis, cv);
    glmc_rotate_make(cd, (float) angle, cv);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_rotate_x_1(FSize const *const mat, FSize const angle, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_rotate_x(cm, (float) angle, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_rotate_x_2(Mat4 const mat, FSize const angle) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_rotate_x(cm, (float) angle, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_rotate_y_1(FSize const *const mat, FSize const angle, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_rotate_y(cm, (float) angle, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_rotate_y_2(Mat4 const mat, FSize const angle) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_rotate_y(cm, (float) angle, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_rotate_z_1(FSize const *const mat, FSize const angle, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_rotate_z(cm, (float) angle, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_rotate_z_2(Mat4 const mat, FSize const angle) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_rotate_z(cm, (float) angle, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_rotated_1(FSize const *const mat, FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    _math_vec3_raw_to_cglm(axis, cv);
    glmc_rotated(cm, (float) angle, cv);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_rotated_2(Mat4 const mat, FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    _math_vec3_to_cglm(axis, cv);
    glmc_rotated(cm, (float) angle, cv);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_rotated_at_1(FSize const *const mat, FSize const *const pivot, FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "pivot", (void*) pivot);
    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cp = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    _math_vec3_raw_to_cglm(pivot, cp);
    _math_vec3_raw_to_cglm(axis, cv);
    glmc_rotated_at(cm, cp, (float) angle, cv);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_rotated_at_2(Mat4 const mat, Vec3 const pivot, FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cp = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    _math_vec3_to_cglm(pivot, cp);
    _math_vec3_to_cglm(axis, cv);
    glmc_rotated_at(cm, cp, (float) angle, cv);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_rotated_x_1(FSize const *const mat, FSize const angle, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_rotated_x(cm, (float) angle, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_rotated_x_2(Mat4 const mat, FSize const angle) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_rotated_x(cm, (float) angle, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_rotated_y_1(FSize const *const mat, FSize const angle, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_rotated_y(cm, (float) angle, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_rotated_y_2(Mat4 const mat, FSize const angle) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_rotated_y(cm, (float) angle, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_rotated_z_1(FSize const *const mat, FSize const angle, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_rotated_z(cm, (float) angle, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_rotated_z_2(Mat4 const mat, FSize const angle) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_rotated_z(cm, (float) angle, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_scale_1(FSize const *const mat, FSize const *const factors, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "factors", (void*) factors);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    _math_vec3_raw_to_cglm(factors, cv);
    glmc_scale(cm, cv);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_scale_2(Mat4 const mat, Vec3 const factors) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    _math_vec3_to_cglm(factors, cv);
    glmc_scale(cm, cv);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_scale_make_1(FSize const *const factors, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "factors", (void*) factors);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(factors, cv);
    glmc_scale_make(cd, cv);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_scale_make_2(Vec3 const factors) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(factors, cv);
    glmc_scale_make(cd, cv);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_scale_uni_1(FSize const *const mat, FSize const factor, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_scale_uni(cm, (float) factor);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_scale_uni_2(Mat4 const mat, FSize const factor) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_scale_uni(cm, (float) factor);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_spin_1(FSize const *const mat, FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    _math_vec3_raw_to_cglm(axis, cv);
    glmc_spin(cm, (float) angle, cv);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_spin_2(Mat4 const mat, FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    _math_vec3_to_cglm(axis, cv);
    glmc_spin(cm, (float) angle, cv);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_spinned_1(FSize const *const mat, FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    _math_vec3_raw_to_cglm(axis, cv);
    glmc_spinned(cm, (float) angle, cv);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_spinned_2(Mat4 const mat, FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    _math_vec3_to_cglm(axis, cv);
    glmc_spinned(cm, (float) angle, cv);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_translate_1(FSize const *const mat, FSize const *const offset, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "offset", (void*) offset);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    _math_vec3_raw_to_cglm(offset, cv);
    glmc_translate(cm, cv);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_translate_2(Mat4 const mat, Vec3 const offset) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    _math_vec3_to_cglm(offset, cv);
    glmc_translate(cm, cv);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_translate_make_1(FSize const *const offset, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "offset", (void*) offset);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(offset, cv);
    glmc_translate_make(cd, cv);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_affine_translate_make_2(Vec3 const offset) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(offset, cv);
    glmc_translate_make(cd, cv);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_affine_translate_x_1(FSize const *const mat, FSize const to, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_translate_x(cm, (float) to);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_translate_x_2(Mat4 const mat, FSize const to) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_translate_x(cm, (float) to);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_translate_y_1(FSize const *const mat, FSize const to, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_translate_y(cm, (float) to);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_translate_y_2(Mat4 const mat, FSize const to) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_translate_y(cm, (float) to);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_translate_z_1(FSize const *const mat, FSize const to, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_translate_z(cm, (float) to);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_translate_z_2(Mat4 const mat, FSize const to) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_translate_z(cm, (float) to);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_translated_1(FSize const *const mat, FSize const *const offset, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "offset", (void*) offset);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    _math_vec3_raw_to_cglm(offset, cv);
    glmc_translated(cm, cv);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_translated_2(Mat4 const mat, Vec3 const offset) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    _math_vec3_to_cglm(offset, cv);
    glmc_translated(cm, cv);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_translated_x_1(FSize const *const mat, FSize const to, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_translated_x(cm, (float) to);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_translated_x_2(Mat4 const mat, FSize const to) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_translated_x(cm, (float) to);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_translated_y_1(FSize const *const mat, FSize const to, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_translated_y(cm, (float) to);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_translated_y_2(Mat4 const mat, FSize const to) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_translated_y(cm, (float) to);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_affine_translated_z_1(FSize const *const mat, FSize const to, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_translated_z(cm, (float) to);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_affine_translated_z_2(Mat4 const mat, FSize const to) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_translated_z(cm, (float) to);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

bool math_affine_uniscaled_1(FSize const *const mat) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);

    bool const result = glmc_uniscaled(cm);

    trace_log_pop();

    return result;
}

bool math_affine_uniscaled_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);

    bool const result = glmc_uniscaled(cm);

    trace_log_pop();

    return result;
}