/*
 * mat3.c - 3x3 matrix operations for the CFW math module.
 *
 * See mat3.h for API documentation and usage examples.
 */

#include <math/mat3.h>

/*==============================================================================
 * MARK: - Mat3 API
 *============================================================================*/

void math_mat3_copy_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_mat3_copy(cm, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat3_copy_2(Mat3 const mat) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_mat3_copy(cm, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_mat3_det_1(FSize const *const mat) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat3_det(cm);

    trace_log_pop();

    return result;
}

FSize math_mat3_det_2(Mat3 const mat) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat3_det(cm);

    trace_log_pop();

    return result;
}

void math_mat3_identity_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cd = DEFAULT_INITIALIZATION;

    glmc_mat3_identity(cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat3_identity_2(void) {
    trace_log_push(LOG_METADATA);

    mat3 cd = DEFAULT_INITIALIZATION;

    glmc_mat3_identity(cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3_inv_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_mat3_inv(cm, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat3_inv_2(Mat3 const mat) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_mat3_inv(cm, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    float cs[9] = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 9; i++) {
        cs[i] = (float) src[i];
    }

    glmc_mat3_make(cs, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat3_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    float cs[9] = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 9; i++) {
        cs[i] = (float) src[i];
    }

    glmc_mat3_make(cs, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 ca = DEFAULT_INITIALIZATION;
    mat3 cb = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(a, ca);
    _math_mat3_raw_to_cglm(b, cb);
    glmc_mat3_mul(ca, cb, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat3_mul_2(Mat3 const a, Mat3 const b) {
    trace_log_push(LOG_METADATA);

    mat3 ca = DEFAULT_INITIALIZATION;
    mat3 cb = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(a, ca);
    _math_mat3_to_cglm(b, cb);
    glmc_mat3_mul(ca, cb, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(m, cm);
    _math_vec3_raw_to_cglm(v, cv);
    glmc_mat3_mulv(cm, cv, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_mat3_mulv_2(Mat3 const m, Vec3 const v) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    vec3 cv = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(m, cm);
    _math_vec3_to_cglm(v, cv);
    glmc_mat3_mulv(cm, cv, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3_quat_1(FSize const *const m, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(m, cm);
    glmc_mat3_quat(cm, cd);
    _math_quat_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Quat math_mat3_quat_2(Mat3 const m) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    versor cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(m, cm);
    glmc_mat3_quat(cm, cd);

    Quat const result = _math_quat_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_mat3_rmc_1(FSize const *const r, FSize const *const m, FSize const *const c) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "c", (void*) c);

    vec3 cr = DEFAULT_INITIALIZATION;
    mat3 cm = DEFAULT_INITIALIZATION;
    vec3 cc = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(r, cr);
    _math_mat3_raw_to_cglm(m, cm);
    _math_vec3_raw_to_cglm(c, cc);

    FSize const result = (FSize) glmc_mat3_rmc(cr, cm, cc);

    trace_log_pop();

    return result;
}

FSize math_mat3_rmc_2(Vec3 const r, Mat3 const m, Vec3 const c) {
    trace_log_push(LOG_METADATA);

    vec3 cr = DEFAULT_INITIALIZATION;
    mat3 cm = DEFAULT_INITIALIZATION;
    vec3 cc = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(r, cr);
    _math_mat3_to_cglm(m, cm);
    _math_vec3_to_cglm(c, cc);

    FSize const result = (FSize) glmc_mat3_rmc(cr, cm, cc);

    trace_log_pop();

    return result;
}

void math_mat3_scale_1(FSize const *const mat, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_mat3_scale(cm, (float) s);
    _math_mat3_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat3 math_mat3_scale_2(Mat3 const mat, FSize const s) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_mat3_scale(cm, (float) s);

    Mat3 const result = _math_mat3_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat3_swap_col_1(FSize const *const mat, ISize const col1, ISize const col2, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);
    error_check_out_of_bound_int(LOG_METADATA, "col1", col1, "columns", (ISize) 3,
            "col1 < 0 || col1 >= columns", col1 < 0 || col1 >= 3);
    error_check_out_of_bound_int(LOG_METADATA, "col2", col2, "columns", (ISize) 3,
            "col2 < 0 || col2 >= columns", col2 < 0 || col2 >= 3);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_mat3_swap_col(cm, (int) col1, (int) col2);
    _math_mat3_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat3 math_mat3_swap_col_2(Mat3 const mat, ISize const col1, ISize const col2) {
    trace_log_push(LOG_METADATA);

    error_check_out_of_bound_int(LOG_METADATA, "col1", col1, "columns", (ISize) 3,
            "col1 < 0 || col1 >= columns", col1 < 0 || col1 >= 3);
    error_check_out_of_bound_int(LOG_METADATA, "col2", col2, "columns", (ISize) 3,
            "col2 < 0 || col2 >= columns", col2 < 0 || col2 >= 3);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_mat3_swap_col(cm, (int) col1, (int) col2);

    Mat3 const result = _math_mat3_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat3_swap_row_1(FSize const *const mat, ISize const row1, ISize const row2, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);
    error_check_out_of_bound_int(LOG_METADATA, "row1", row1, "rows", (ISize) 3,
            "row1 < 0 || row1 >= rows", row1 < 0 || row1 >= 3);
    error_check_out_of_bound_int(LOG_METADATA, "row2", row2, "rows", (ISize) 3,
            "row2 < 0 || row2 >= rows", row2 < 0 || row2 >= 3);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_mat3_swap_row(cm, (int) row1, (int) row2);
    _math_mat3_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat3 math_mat3_swap_row_2(Mat3 const mat, ISize const row1, ISize const row2) {
    trace_log_push(LOG_METADATA);

    error_check_out_of_bound_int(LOG_METADATA, "row1", row1, "rows", (ISize) 3,
            "row1 < 0 || row1 >= rows", row1 < 0 || row1 >= 3);
    error_check_out_of_bound_int(LOG_METADATA, "row2", row2, "rows", (ISize) 3,
            "row2 < 0 || row2 >= rows", row2 < 0 || row2 >= 3);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_mat3_swap_row(cm, (int) row1, (int) row2);

    Mat3 const result = _math_mat3_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat3_textrans_1(FSize const sx, FSize const sy, FSize const rot, FSize const tx, FSize const ty, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cd = DEFAULT_INITIALIZATION;

    _math_cglm_mat3_textrans((float) sx, (float) sy, (float) rot, (float) tx, (float) ty, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat3_textrans_2(FSize const sx, FSize const sy, FSize const rot, FSize const tx, FSize const ty) {
    trace_log_push(LOG_METADATA);

    mat3 cd = DEFAULT_INITIALIZATION;

    _math_cglm_mat3_textrans((float) sx, (float) sy, (float) rot, (float) tx, (float) ty, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_mat3_trace_1(FSize const *const mat) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat3_trace(cm);

    trace_log_pop();

    return result;
}

FSize math_mat3_trace_2(Mat3 const mat) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat3_trace(cm);

    trace_log_pop();

    return result;
}

void math_mat3_transpose_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_raw_to_cglm(mat, cm);
    glmc_mat3_transpose_to(cm, cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat3_transpose_2(Mat3 const mat) {
    trace_log_push(LOG_METADATA);

    mat3 cm = DEFAULT_INITIALIZATION;
    mat3 cd = DEFAULT_INITIALIZATION;

    _math_mat3_to_cglm(mat, cm);
    glmc_mat3_transpose_to(cm, cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat3_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat3 cd = DEFAULT_INITIALIZATION;

    glmc_mat3_zero(cd);
    _math_mat3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat3 math_mat3_zero_2(void) {
    trace_log_push(LOG_METADATA);

    mat3 cd = DEFAULT_INITIALIZATION;

    glmc_mat3_zero(cd);

    Mat3 const result = _math_mat3_from_cglm(cd);

    trace_log_pop();

    return result;
}