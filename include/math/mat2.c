/*
 * mat2.c - 2x2 matrix operations for the CFW math module.
 *
 * See mat2.h for API documentation and usage examples.
 */

#include <math/mat2.h>

/*==============================================================================
 * MARK: - Mat2 API
 *============================================================================*/

void math_mat2_copy_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2 cm = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(mat, cm);
    glmc_mat2_copy(cm, cd);
    _math_mat2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2 math_mat2_copy_2(Mat2 const mat) {
    trace_log_push(LOG_METADATA);

    mat2 cm = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(mat, cm);
    glmc_mat2_copy(cm, cd);

    Mat2 const result = _math_mat2_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_mat2_det_1(FSize const *const mat) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat2_det(cm);

    trace_log_pop();

    return result;
}

FSize math_mat2_det_2(Mat2 const mat) {
    trace_log_push(LOG_METADATA);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat2_det(cm);

    trace_log_pop();

    return result;
}

void math_mat2_identity_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2 cd = DEFAULT_INITIALIZATION;

    glmc_mat2_identity(cd);
    _math_mat2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2 math_mat2_identity_2(void) {
    trace_log_push(LOG_METADATA);

    mat2 cd = DEFAULT_INITIALIZATION;

    glmc_mat2_identity(cd);

    Mat2 const result = _math_mat2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2_inv_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2 cm = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(mat, cm);
    glmc_mat2_inv(cm, cd);
    _math_mat2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2 math_mat2_inv_2(Mat2 const mat) {
    trace_log_push(LOG_METADATA);

    mat2 cm = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(mat, cm);
    glmc_mat2_inv(cm, cd);

    Mat2 const result = _math_mat2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2_make_1(FSize const *const src, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    float cs[4] = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 4; i++) {
        cs[i] = (float) src[i];
    }

    glmc_mat2_make(cs, cd);
    _math_mat2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2 math_mat2_make_2(FSize const *const src) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "src", (void*) src);

    float cs[4] = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 4; i++) {
        cs[i] = (float) src[i];
    }

    glmc_mat2_make(cs, cd);

    Mat2 const result = _math_mat2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2_mul_1(FSize const *const a, FSize const *const b, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "a", (void*) a);
    error_check_null(LOG_METADATA, "b", (void*) b);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2 cm1 = DEFAULT_INITIALIZATION;
    mat2 cm2 = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(a, cm1);
    _math_mat2_raw_to_cglm(b, cm2);
    glmc_mat2_mul(cm1, cm2, cd);
    _math_mat2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2 math_mat2_mul_2(Mat2 const a, Mat2 const b) {
    trace_log_push(LOG_METADATA);

    mat2 cm1 = DEFAULT_INITIALIZATION;
    mat2 cm2 = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(a, cm1);
    _math_mat2_to_cglm(b, cm2);
    glmc_mat2_mul(cm1, cm2, cd);

    Mat2 const result = _math_mat2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2_mulv_1(FSize const *const m, FSize const *const v, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "v", (void*) v);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2 cm = DEFAULT_INITIALIZATION;
    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(m, cm);
    _math_vec2_raw_to_cglm(v, cv);
    glmc_mat2_mulv(cm, cv, cd);
    _math_vec2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec2 math_mat2_mulv_2(Mat2 const m, Vec2 const v) {
    trace_log_push(LOG_METADATA);

    mat2 cm = DEFAULT_INITIALIZATION;
    vec2 cv = DEFAULT_INITIALIZATION;
    vec2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(m, cm);
    _math_vec2_to_cglm(v, cv);
    glmc_mat2_mulv(cm, cv, cd);

    Vec2 const result = _math_vec2_from_cglm(cd);

    trace_log_pop();

    return result;
}

FSize math_mat2_rmc_1(FSize const *const r, FSize const *const m, FSize const *const c) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "r", (void*) r);
    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "c", (void*) c);

    vec2 cr = DEFAULT_INITIALIZATION;
    mat2 cm = DEFAULT_INITIALIZATION;
    vec2 cc = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(r, cr);
    _math_mat2_raw_to_cglm(m, cm);
    _math_vec2_raw_to_cglm(c, cc);

    FSize const result = (FSize) glmc_mat2_rmc(cr, cm, cc);

    trace_log_pop();

    return result;
}

FSize math_mat2_rmc_2(Vec2 const r, Mat2 const m, Vec2 const c) {
    trace_log_push(LOG_METADATA);

    vec2 cr = DEFAULT_INITIALIZATION;
    mat2 cm = DEFAULT_INITIALIZATION;
    vec2 cc = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(r, cr);
    _math_mat2_to_cglm(m, cm);
    _math_vec2_to_cglm(c, cc);

    FSize const result = (FSize) glmc_mat2_rmc(cr, cm, cc);

    trace_log_pop();

    return result;
}

void math_mat2_scale_1(FSize const *const mat, FSize const s, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(mat, cm);
    glmc_mat2_scale(cm, (float) s);
    _math_mat2_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat2 math_mat2_scale_2(Mat2 const mat, FSize const s) {
    trace_log_push(LOG_METADATA);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(mat, cm);
    glmc_mat2_scale(cm, (float) s);

    Mat2 const result = _math_mat2_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat2_swap_col_1(FSize const *const mat, ISize const col1, ISize const col2, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);
    error_check_out_of_bound_int(LOG_METADATA, "col1", col1, "columns", (ISize) 2,
            "col1 < 0 || col1 >= columns", col1 < 0 || col1 >= 2);
    error_check_out_of_bound_int(LOG_METADATA, "col2", col2, "columns", (ISize) 2,
            "col2 < 0 || col2 >= columns", col2 < 0 || col2 >= 2);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(mat, cm);
    glmc_mat2_swap_col(cm, (int) col1, (int) col2);
    _math_mat2_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat2 math_mat2_swap_col_2(Mat2 const mat, ISize const col1, ISize const col2) {
    trace_log_push(LOG_METADATA);

    error_check_out_of_bound_int(LOG_METADATA, "col1", col1, "columns", (ISize) 2,
            "col1 < 0 || col1 >= columns", col1 < 0 || col1 >= 2);
    error_check_out_of_bound_int(LOG_METADATA, "col2", col2, "columns", (ISize) 2,
            "col2 < 0 || col2 >= columns", col2 < 0 || col2 >= 2);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(mat, cm);
    glmc_mat2_swap_col(cm, (int) col1, (int) col2);

    Mat2 const result = _math_mat2_from_cglm(cm);

    trace_log_pop();

    return result;
}

void math_mat2_swap_row_1(FSize const *const mat, ISize const row1, ISize const row2, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);
    error_check_out_of_bound_int(LOG_METADATA, "row1", row1, "rows", (ISize) 2,
            "row1 < 0 || row1 >= rows", row1 < 0 || row1 >= 2);
    error_check_out_of_bound_int(LOG_METADATA, "row2", row2, "rows", (ISize) 2,
            "row2 < 0 || row2 >= rows", row2 < 0 || row2 >= 2);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(mat, cm);
    glmc_mat2_swap_row(cm, (int) row1, (int) row2);
    _math_mat2_raw_from_cglm(cm, dest);

    trace_log_pop();
}

Mat2 math_mat2_swap_row_2(Mat2 const mat, ISize const row1, ISize const row2) {
    trace_log_push(LOG_METADATA);

    error_check_out_of_bound_int(LOG_METADATA, "row1", row1, "rows", (ISize) 2,
            "row1 < 0 || row1 >= rows", row1 < 0 || row1 >= 2);
    error_check_out_of_bound_int(LOG_METADATA, "row2", row2, "rows", (ISize) 2,
            "row2 < 0 || row2 >= rows", row2 < 0 || row2 >= 2);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(mat, cm);
    glmc_mat2_swap_row(cm, (int) row1, (int) row2);

    Mat2 const result = _math_mat2_from_cglm(cm);

    trace_log_pop();

    return result;
}

FSize math_mat2_trace_1(FSize const *const mat) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat2_trace(cm);

    trace_log_pop();

    return result;
}

FSize math_mat2_trace_2(Mat2 const mat) {
    trace_log_push(LOG_METADATA);

    mat2 cm = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(mat, cm);

    FSize const result = (FSize) glmc_mat2_trace(cm);

    trace_log_pop();

    return result;
}

void math_mat2_transpose_1(FSize const *const mat, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "mat", (void*) mat);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2 cm = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_raw_to_cglm(mat, cm);
    glmc_mat2_transpose_to(cm, cd);
    _math_mat2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2 math_mat2_transpose_2(Mat2 const mat) {
    trace_log_push(LOG_METADATA);

    mat2 cm = DEFAULT_INITIALIZATION;
    mat2 cd = DEFAULT_INITIALIZATION;

    _math_mat2_to_cglm(mat, cm);
    glmc_mat2_transpose_to(cm, cd);

    Mat2 const result = _math_mat2_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_mat2_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat2 cd = DEFAULT_INITIALIZATION;

    glmc_mat2_zero(cd);
    _math_mat2_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Mat2 math_mat2_zero_2(void) {
    trace_log_push(LOG_METADATA);

    mat2 cd = DEFAULT_INITIALIZATION;

    glmc_mat2_zero(cd);

    Mat2 const result = _math_mat2_from_cglm(cd);

    trace_log_pop();

    return result;
}