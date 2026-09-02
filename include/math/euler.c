/*
 * euler.c - Euler-angle rotation builders for the CFW math module.
 *
 * See euler.h for API documentation and usage examples.
 */

#include <math/euler.h>

/*==============================================================================
 * MARK: - Euler API
 *============================================================================*/

void math_euler_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler(ca, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_euler_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler(ca, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_angles_1(FSize const *const m, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    glmc_euler_angles(cm, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_euler_angles_2(Mat4 const m) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    glmc_euler_angles(cm, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_by_order_1(FSize const *const angles, MathEulerOrder const order, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    /* LOAD-BEARING zero: cglm's glm_euler_by_order switch has no default, so an order outside
     * the enum leaves the 3x3 block untouched - this initialiser is what makes that a zero
     * matrix rather than an uninitialised-stack read. */
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_by_order(ca, (glm_euler_seq) order, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_euler_by_order_2(Vec3 const angles, MathEulerOrder const order) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    /* LOAD-BEARING zero: cglm's glm_euler_by_order switch has no default, so an order outside
     * the enum leaves the 3x3 block untouched - this initialiser is what makes that a zero
     * matrix rather than an uninitialised-stack read. */
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_by_order(ca, (glm_euler_seq) order, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_xyz_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_xyz(ca, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_euler_xyz_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_xyz(ca, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_xyz_quat_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_xyz_quat(ca, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_euler_xyz_quat_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_xyz_quat(ca, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_xzy_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_xzy(ca, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_euler_xzy_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_xzy(ca, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_xzy_quat_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_xzy_quat(ca, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_euler_xzy_quat_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_xzy_quat(ca, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_yxz_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_yxz(ca, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_euler_yxz_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_yxz(ca, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_yxz_quat_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_yxz_quat(ca, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_euler_yxz_quat_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_yxz_quat(ca, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_yzx_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_yzx(ca, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_euler_yzx_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_yzx(ca, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_yzx_quat_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_yzx_quat(ca, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_euler_yzx_quat_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_yzx_quat(ca, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_zxy_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_zxy(ca, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_euler_zxy_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_zxy(ca, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_zxy_quat_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_zxy_quat(ca, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_euler_zxy_quat_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_zxy_quat(ca, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_zyx_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_zyx(ca, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_euler_zyx_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_zyx(ca, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_euler_zyx_quat_1(FSize const *const angles, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "angles", (void*) angles);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(angles, ca);
    glmc_euler_zyx_quat(ca, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_euler_zyx_quat_2(Vec3 const angles) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(angles, ca);
    glmc_euler_zyx_quat(ca, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}