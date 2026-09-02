/*
 * mat4.c - 4x4 matrix operations for the CFW math module.
 *
 * See mat4.h for API documentation and usage examples.
 */

#include <math/mat4.h>

/*==============================================================================
 * MARK: - Mat4 API
 *============================================================================*/

void math_mat4_copy_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_copy(cm, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_copy_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_copy(cm, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_mat4_det_1(FSize const *const mat) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat4_det(cm);

    trace_log_pop();

    return result;
}

FSize math_mat4_det_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat4_det(cm);

    trace_log_pop();

    return result;
}

void math_mat4_identity_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_mat4_identity(cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_identity_2(void) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_mat4_identity(cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_ins3_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    _math_mat4_raw_to_cglm(dest, cd);
    glmc_mat4_ins3(cm, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_ins3_2(Mat3 const mat, Mat4 const accumulator) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    _math_mat4_to_cglm(accumulator, cd);
    glmc_mat4_ins3(cm, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_inv_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_inv(cm, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_inv_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_inv(cm, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_inv_fast_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_inv_fast(cm, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_inv_fast_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_inv_fast(cm, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_inv_precise_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_inv_precise(cm, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_inv_precise_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_inv_precise(cm, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    float cs[16] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 16; i++) {
        cs[i] = (float) src[i];
    }

    glmc_mat4_make(cs, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    float cs[16] = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 16; i++) {
        cs[i] = (float) src[i];
    }

    glmc_mat4_make(cs, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 ca = DEFAULT_INITIALIZATION;
    mat4 cb = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(a, ca);
    _math_mat4_raw_to_cglm(b, cb);
    glmc_mat4_mul(ca, cb, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_mul_2(Mat4 const a, Mat4 const b) {
    trace_log_push(LOG_METADATA);

    mat4 ca = DEFAULT_INITIALIZATION;
    mat4 cb = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(a, ca);
    _math_mat4_to_cglm(b, cb);
    glmc_mat4_mul(ca, cb, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    _math_vec4_raw_to_cglm(v, cv);
    glmc_mat4_mulv(cm, cv, cd);
    _math_vec4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec4 math_mat4_mulv_2(Mat4 const m, Vec4 const v) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cv = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    _math_vec4_to_cglm(v, cv);
    glmc_mat4_mulv(cm, cv, cd);

    Vec4 const result = _math_vec4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_mulv3_1(FSize const *const m, FSize const *const v, FSize const last, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    _math_vec3_raw_to_cglm(v, cv);
    glmc_mat4_mulv3(cm, cv, (float) last, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_mat4_mulv3_2(Mat4 const m, Vec3 const v, FSize const last) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    _math_vec3_to_cglm(v, cv);
    glmc_mat4_mulv3(cm, cv, (float) last, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_pick3_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_pick3(cm, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat4_pick3_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_pick3(cm, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_pick3t_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_pick3t(cm, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat4_pick3t_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_pick3t(cm, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_quat_1(FSize const *const m, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    glmc_mat4_quat(cm, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_mat4_quat_2(Mat4 const m) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    glmc_mat4_quat(cm, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_mat4_rmc_1(FSize const *const r, FSize const *const m, FSize const *const c) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "c", (void*) c);

    vec4 cr = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cc = DEFAULT_INITIALIZATION;

    _math_vec4_raw_to_cglm(r, cr);
    _math_mat4_raw_to_cglm(m, cm);
    _math_vec4_raw_to_cglm(c, cc);

    FSize const result = (FSize) glmc_mat4_rmc(cr, cm, cc);

    trace_log_pop();

    return result;
}

FSize math_mat4_rmc_2(Vec4 const r, Mat4 const m, Vec4 const c) {
    trace_log_push(LOG_METADATA);

    vec4 cr = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cc = DEFAULT_INITIALIZATION;

    _math_vec4_to_cglm(r, cr);
    _math_mat4_to_cglm(m, cm);
    _math_vec4_to_cglm(c, cc);

    FSize const result = (FSize) glmc_mat4_rmc(cr, cm, cc);

    trace_log_pop();

    return result;
}

void math_mat4_scale_1(FSize const *const mat, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_scale(cm, (float) s);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_mat4_scale_2(Mat4 const mat, FSize const s) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_scale(cm, (float) s);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat4_scale_p_1(FSize const *const m, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    glmc_mat4_scale_p(cm, (float) s);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_mat4_scale_p_2(Mat4 const m, FSize const s) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    glmc_mat4_scale_p(cm, (float) s);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat4_swap_col_1(FSize const *const mat, ISize const col1, ISize const col2, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);
    error_check_out_of_bound_int(LOG_METADATA, "col1", col1, "columns", (ISize) 4,
            "col1 < 0 || col1 >= columns", col1 < 0 || col1 >= 4);
    error_check_out_of_bound_int(LOG_METADATA, "col2", col2, "columns", (ISize) 4,
            "col2 < 0 || col2 >= columns", col2 < 0 || col2 >= 4);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_swap_col(cm, (int) col1, (int) col2);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_mat4_swap_col_2(Mat4 const mat, ISize const col1, ISize const col2) {
    trace_log_push(LOG_METADATA);

    error_check_out_of_bound_int(LOG_METADATA, "col1", col1, "columns", (ISize) 4,
            "col1 < 0 || col1 >= columns", col1 < 0 || col1 >= 4);
    error_check_out_of_bound_int(LOG_METADATA, "col2", col2, "columns", (ISize) 4,
            "col2 < 0 || col2 >= columns", col2 < 0 || col2 >= 4);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_swap_col(cm, (int) col1, (int) col2);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat4_swap_row_1(FSize const *const mat, ISize const row1, ISize const row2, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);
    error_check_out_of_bound_int(LOG_METADATA, "row1", row1, "rows", (ISize) 4,
            "row1 < 0 || row1 >= rows", row1 < 0 || row1 >= 4);
    error_check_out_of_bound_int(LOG_METADATA, "row2", row2, "rows", (ISize) 4,
            "row2 < 0 || row2 >= rows", row2 < 0 || row2 >= 4);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_swap_row(cm, (int) row1, (int) row2);
    _math_mat4_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat4 math_mat4_swap_row_2(Mat4 const mat, ISize const row1, ISize const row2) {
    trace_log_push(LOG_METADATA);

    error_check_out_of_bound_int(LOG_METADATA, "row1", row1, "rows", (ISize) 4,
            "row1 < 0 || row1 >= rows", row1 < 0 || row1 >= 4);
    error_check_out_of_bound_int(LOG_METADATA, "row2", row2, "rows", (ISize) 4,
            "row2 < 0 || row2 >= rows", row2 < 0 || row2 >= 4);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_swap_row(cm, (int) row1, (int) row2);

    Mat4 const result = _math_mat4_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat4_textrans_1(FSize const sx, FSize const sy, FSize const rot, FSize const tx, FSize const ty, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    _math_cglm_mat4_textrans((float) sx, (float) sy, (float) rot, (float) tx, (float) ty, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_textrans_2(FSize const sx, FSize const sy, FSize const rot, FSize const tx, FSize const ty) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    _math_cglm_mat4_textrans((float) sx, (float) sy, (float) rot, (float) tx, (float) ty, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_mat4_trace_1(FSize const *const mat) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat4_trace(cm);

    trace_log_pop();

    return result;
}

FSize math_mat4_trace_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat4_trace(cm);

    trace_log_pop();

    return result;
}

FSize math_mat4_trace3_1(FSize const *const m) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);

    FSize const result = (FSize) glmc_mat4_trace3(cm);

    trace_log_pop();

    return result;
}

FSize math_mat4_trace3_2(Mat4 const m) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);

    FSize const result = (FSize) glmc_mat4_trace3(cm);

    trace_log_pop();

    return result;
}

void math_mat4_transpose_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(mat, cm);
    glmc_mat4_transpose_to(cm, cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_transpose_2(Mat4 const mat) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    mat4 cd = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(mat, cm);
    glmc_mat4_transpose_to(cm, cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat4_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_mat4_zero(cd);
    _math_mat4_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat4 math_mat4_zero_2(void) {
    trace_log_push(LOG_METADATA);

    mat4 cd = DEFAULT_INITIALIZATION;

    glmc_mat4_zero(cd);

    Mat4 const result = _math_mat4_from_cglm(cd);

    trace_log_pop();

    return result;
}