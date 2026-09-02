/*
 * quat.c - Quaternion operations for the CFW math module.
 *
 * See quat.h for API documentation and usage examples.
 */

#include <math/quat.h>

/*==============================================================================
 * MARK: - Quat API
 *============================================================================*/

void math_quat_add_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor ca = DEFAULT_INITIALIZATION;
    versor cb = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(a, ca);
    _math_quat_raw_to_cglm(b, cb);
    glmc_quat_add(ca, cb, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_add_2(Quat const a, Quat const b) {
    trace_log_push(LOG_METADATA);

    versor ca = DEFAULT_INITIALIZATION;
    versor cb = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(a, ca);
    _math_quat_to_cglm(b, cb);
    glmc_quat_add(ca, cb, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_quat_angle_1(FSize const *const q) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);

    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_angle(cq);

    trace_log_pop();

    return result;
}

FSize math_quat_angle_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_angle(cq);

    trace_log_pop();

    return result;
}

void math_quat_axis_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_axis(cq, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_quat_axis_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_axis(cq, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_conjugate_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_conjugate(cq, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_conjugate_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_conjugate(cq, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_copy_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_copy(cq, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_copy_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_copy(cq, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_quat_dot_1(FSize const *const p, FSize const *const q) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "p", (void*) p);
    error_check_null(LOG_METADATA, "q", (void*) q);

    versor cp = DEFAULT_INITIALIZATION;
    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(p, cp);
    _math_quat_raw_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_dot(cp, cq);

    trace_log_pop();

    return result;
}

FSize math_quat_dot_2(Quat const p, Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cp = DEFAULT_INITIALIZATION;
    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(p, cp);
    _math_quat_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_dot(cp, cq);

    trace_log_pop();

    return result;
}

void math_quat_for_1(FSize const *const dir, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dir", (void*) dir);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cdir = DEFAULT_INITIALIZATION;
    vec3 cup = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(dir, cdir);
    _math_vec3_raw_to_cglm(up, cup);
    glmc_quat_for(cdir, cup, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_for_2(Vec3 const dir, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 cdir = DEFAULT_INITIALIZATION;
    vec3 cup = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(dir, cdir);
    _math_vec3_to_cglm(up, cup);
    glmc_quat_for(cdir, cup, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_forp_1(FSize const *const from, FSize const *const to, FSize const *const up, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "up", (void*) up);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cfrom = DEFAULT_INITIALIZATION;
    vec3 cto = DEFAULT_INITIALIZATION;
    vec3 cup = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(from, cfrom);
    _math_vec3_raw_to_cglm(to, cto);
    _math_vec3_raw_to_cglm(up, cup);
    glmc_quat_forp(cfrom, cto, cup, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_forp_2(Vec3 const from, Vec3 const to, Vec3 const up) {
    trace_log_push(LOG_METADATA);

    vec3 cfrom = DEFAULT_INITIALIZATION;
    vec3 cto = DEFAULT_INITIALIZATION;
    vec3 cup = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(from, cfrom);
    _math_vec3_to_cglm(to, cto);
    _math_vec3_to_cglm(up, cup);
    glmc_quat_forp(cfrom, cto, cup, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_from_vecs_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(a, ca);
    _math_vec3_raw_to_cglm(b, cb);
    glmc_quat_from_vecs(ca, cb, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_from_vecs_2(Vec3 const a, Vec3 const b) {
    trace_log_push(LOG_METADATA);

    vec3 ca = DEFAULT_INITIALIZATION;
    vec3 cb = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(a, ca);
    _math_vec3_to_cglm(b, cb);
    glmc_quat_from_vecs(ca, cb, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_identity_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;

    glmc_quat_identity(cq);
    _math_quat_raw_from_cglm(cq, dest);

    trace_log_pop();
}

Quat math_quat_identity_2(void) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;

    glmc_quat_identity(cq);

    Quat const result = _math_quat_from_cglm(cq);

    trace_log_pop();

    return result;
}

void math_quat_imag_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_imag(cq, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_quat_imag_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_imag(cq, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_quat_imaglen_1(FSize const *const q) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);

    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_imaglen(cq);

    trace_log_pop();

    return result;
}

FSize math_quat_imaglen_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_imaglen(cq);

    trace_log_pop();

    return result;
}

void math_quat_imagn_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_imagn(cq, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_quat_imagn_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_imagn(cq, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_init_1(FSize const x, FSize const y, FSize const z, FSize const w, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cd = DEFAULT_INITIALIZATION;

    glmc_quat_init(cd, (float) x, (float) y, (float) z, (float) w);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_init_2(FSize const x, FSize const y, FSize const z, FSize const w) {
    trace_log_push(LOG_METADATA);

    versor cd = DEFAULT_INITIALIZATION;

    glmc_quat_init(cd, (float) x, (float) y, (float) z, (float) w);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_inv_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_inv(cq, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_inv_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_inv(cq, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cfrom = DEFAULT_INITIALIZATION;
    versor cto = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(from, cfrom);
    _math_quat_raw_to_cglm(to, cto);
    glmc_quat_lerp(cfrom, cto, (float) t, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_lerp_2(Quat const from, Quat const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    versor cfrom = DEFAULT_INITIALIZATION;
    versor cto = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(from, cfrom);
    _math_quat_to_cglm(to, cto);
    glmc_quat_lerp(cfrom, cto, (float) t, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_lerpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "from", (void*) from);
    error_check_null(LOG_METADATA, "to", (void*) to);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cfrom = DEFAULT_INITIALIZATION;
    versor cto = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(from, cfrom);
    _math_quat_raw_to_cglm(to, cto);
    glmc_quat_lerpc(cfrom, cto, (float) t, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_lerpc_2(Quat const from, Quat const to, FSize const t) {
    trace_log_push(LOG_METADATA);

    versor cfrom = DEFAULT_INITIALIZATION;
    versor cto = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(from, cfrom);
    _math_quat_to_cglm(to, cto);
    glmc_quat_lerpc(cfrom, cto, (float) t, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_look_1(FSize const *const eye, FSize const *const ori, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "eye", (void*) eye);
    error_check_null(LOG_METADATA, "ori", (void*) ori);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 ceye = DEFAULT_INITIALIZATION;
    versor cori = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(eye, ceye);
    _math_quat_raw_to_cglm(ori, cori);
    glmc_quat_look(ceye, cori, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_quat_look_2(Vec3 const eye, Quat const ori) {
    trace_log_push(LOG_METADATA);

    vec3 ceye = DEFAULT_INITIALIZATION;
    versor cori = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(eye, ceye);
    _math_quat_to_cglm(ori, cori);
    glmc_quat_look(ceye, cori, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cs = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(src, cs);
    glmc_quat_make(cs, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    versor cs = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(src, cs);
    glmc_quat_make(cs, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_mat3_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_mat3(cq, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_quat_mat3_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_mat3(cq, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_mat3t_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_mat3t(cq, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_quat_mat3t_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_mat3t(cq, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_mat4_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_mat4(cq, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_quat_mat4_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_mat4(cq, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_mat4t_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_mat4t(cq, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_quat_mat4t_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_mat4t(cq, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor ca = DEFAULT_INITIALIZATION;
    versor cb = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(a, ca);
    _math_quat_raw_to_cglm(b, cb);
    glmc_quat_mul(ca, cb, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_mul_2(Quat const a, Quat const b) {
    trace_log_push(LOG_METADATA);

    versor ca = DEFAULT_INITIALIZATION;
    versor cb = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(a, ca);
    _math_quat_to_cglm(b, cb);
    glmc_quat_mul(ca, cb, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_nlerp_1(FSize const *const q, FSize const *const r, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    versor cr = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    _math_quat_raw_to_cglm(r, cr);
    glmc_quat_nlerp(cq, cr, (float) t, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_nlerp_2(Quat const q, Quat const r, FSize const t) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    versor cr = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    _math_quat_to_cglm(r, cr);
    glmc_quat_nlerp(cq, cr, (float) t, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_quat_norm_1(FSize const *const q) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);

    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_norm(cq);

    trace_log_pop();

    return result;
}

FSize math_quat_norm_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_norm(cq);

    trace_log_pop();

    return result;
}

void math_quat_normalize_1(FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_normalize_to(cq, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_normalize_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    glmc_quat_normalize_to(cq, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_quat_1(FSize const angle, FSize const x, FSize const y, FSize const z, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cd = DEFAULT_INITIALIZATION;

    glmc_quat(cd, (float) angle, (float) x, (float) y, (float) z);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_quat_2(FSize const angle, FSize const x, FSize const y, FSize const z) {
    trace_log_push(LOG_METADATA);

    versor cd = DEFAULT_INITIALIZATION;

    glmc_quat(cd, (float) angle, (float) x, (float) y, (float) z);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_quatv_1(FSize const angle, FSize const *const axis, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "axis", (void*) axis);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cv = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(axis, cv);
    glmc_quatv(cd, (float) angle, cv);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_quatv_2(FSize const angle, Vec3 const axis) {
    trace_log_push(LOG_METADATA);

    vec3 cv = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(axis, cv);
    glmc_quatv(cd, (float) angle, cv);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_quat_real_1(FSize const *const q) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);

    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_real(cq);

    trace_log_pop();

    return result;
}

FSize math_quat_real_2(Quat const q) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);

    FSize const result = (FSize) glmc_quat_real(cq);

    trace_log_pop();

    return result;
}

void math_quat_rotate_1(FSize const *const m, FSize const *const q, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    versor cq = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    _math_quat_raw_to_cglm(q, cq);
    glmc_quat_rotate(cm, cq, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_quat_rotate_2(Mat4 const m, Quat const q) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    versor cq = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    _math_quat_to_cglm(q, cq);
    glmc_quat_rotate(cm, cq, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_rotate_at_1(FSize const *const m, FSize const *const q, FSize const *const pivot, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "pivot", (void*) pivot);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    versor cq = DEFAULT_INITIALIZATION;
    vec3 cpivot = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    _math_quat_raw_to_cglm(q, cq);
    _math_vec3_raw_to_cglm(pivot, cpivot);
    glmc_quat_rotate_at(cm, cq, cpivot);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_quat_rotate_at_2(Mat4 const m, Quat const q, Vec3 const pivot) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    versor cq = DEFAULT_INITIALIZATION;
    vec3 cpivot = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    _math_quat_to_cglm(q, cq);
    _math_vec3_to_cglm(pivot, cpivot);
    glmc_quat_rotate_at(cm, cq, cpivot);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_quat_rotate_atm_1(FSize const *const m, FSize const *const q, FSize const *const pivot, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "pivot", (void*) pivot);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    versor cq = DEFAULT_INITIALIZATION;
    vec3 cpivot = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    _math_quat_raw_to_cglm(q, cq);
    _math_vec3_raw_to_cglm(pivot, cpivot);
    glmc_quat_rotate_atm(cm, cq, cpivot);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_quat_rotate_atm_2(Mat4 const m, Quat const q, Vec3 const pivot) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    versor cq = DEFAULT_INITIALIZATION;
    vec3 cpivot = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    _math_quat_to_cglm(q, cq);
    _math_vec3_to_cglm(pivot, cpivot);
    glmc_quat_rotate_atm(cm, cq, cpivot);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_quat_rotatev_1(FSize const *const q, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cfrom = DEFAULT_INITIALIZATION;
    vec3 cto = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cfrom);
    _math_vec3_raw_to_cglm(v, cto);
    glmc_quat_rotatev(cfrom, cto, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_quat_rotatev_2(Quat const q, Vec3 const v) {
    trace_log_push(LOG_METADATA);

    versor cfrom = DEFAULT_INITIALIZATION;
    vec3 cto = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cfrom);
    _math_vec3_to_cglm(v, cto);
    glmc_quat_rotatev(cfrom, cto, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_slerp_1(FSize const *const q, FSize const *const r, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    versor cr = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    _math_quat_raw_to_cglm(r, cr);
    glmc_quat_slerp(cq, cr, (float) t, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_slerp_2(Quat const q, Quat const r, FSize const t) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    versor cr = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    _math_quat_to_cglm(r, cr);
    glmc_quat_slerp(cq, cr, (float) t, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_slerp_longest_1(FSize const *const q, FSize const *const r, FSize const t, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "q", (void*) q);
    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor cq = DEFAULT_INITIALIZATION;
    versor cr = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(q, cq);
    _math_quat_raw_to_cglm(r, cr);
    glmc_quat_slerp_longest(cq, cr, (float) t, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_slerp_longest_2(Quat const q, Quat const r, FSize const t) {
    trace_log_push(LOG_METADATA);

    versor cq = DEFAULT_INITIALIZATION;
    versor cr = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(q, cq);
    _math_quat_to_cglm(r, cr);
    glmc_quat_slerp_longest(cq, cr, (float) t, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_quat_sub_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    versor ca = DEFAULT_INITIALIZATION;
    versor cb = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_raw_to_cglm(a, ca);
    _math_quat_raw_to_cglm(b, cb);
    glmc_quat_sub(ca, cb, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_quat_sub_2(Quat const a, Quat const b) {
    trace_log_push(LOG_METADATA);

    versor ca = DEFAULT_INITIALIZATION;
    versor cb = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_quat_to_cglm(a, ca);
    _math_quat_to_cglm(b, cb);
    glmc_quat_sub(ca, cb, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}