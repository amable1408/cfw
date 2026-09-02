/*
 * frustum.c - View-frustum extraction for the CFW math module.
 *
 * See frustum.h for API documentation and usage examples.
 */

#include <math/frustum.h>

/*==============================================================================
 * MARK: - Frustum API
 *============================================================================*/

void math_frustum_box_1(FSize const *const corners, FSize const *const m, FSize *const box_dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "corners", (void*) corners);
    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "box_dest", (void*) box_dest);

    vec4 cc[8] = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 8; i++) {
        _math_vec4_raw_to_cglm(corners + i * 4, cc[i]);
    }

    _math_mat4_raw_to_cglm(m, cm);
    glmc_frustum_box(cc, cm, cbox);
    _math_vec3_raw_from_cglm(cbox[0], box_dest);
    _math_vec3_raw_from_cglm(cbox[1], box_dest + 3);

    trace_log_pop();
}

Box math_frustum_box_2(FrustumCorners const corners, Mat4 const m) {
    trace_log_push(LOG_METADATA);

    vec4 cc[8] = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    _math_frustum_corners_to_cglm(corners, cc);
    _math_mat4_to_cglm(m, cm);
    glmc_frustum_box(cc, cm, cbox);

    Box const result = { _math_vec3_from_cglm(cbox[0]), _math_vec3_from_cglm(cbox[1]) };

    trace_log_pop();

    return result;
}

void math_frustum_center_1(FSize const *const corners, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "corners", (void*) corners);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cc[8] = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 8; i++) {
        _math_vec4_raw_to_cglm(corners + i * 4, cc[i]);
    }

    glmc_frustum_center(cc, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_frustum_center_2(FrustumCorners const corners) {
    trace_log_push(LOG_METADATA);

    vec4 cc[8] = DEFAULT_INITIALIZATION;
    vec4 cd = DEFAULT_INITIALIZATION;

    _math_frustum_corners_to_cglm(corners, cc);
    glmc_frustum_center(cc, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_frustum_corners_1(FSize const *const inv, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "inv", (void*) inv);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cd[8] = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(inv, cm);
    glmc_frustum_corners(cm, cd);

    for (USize i = 0; i < 8; i++) {
        _math_vec4_raw_from_cglm(cd[i], dest + i * 4);
    }

    trace_log_pop();
}

FrustumCorners math_frustum_corners_2(Mat4 const inv) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cd[8] = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(inv, cm);
    glmc_frustum_corners(cm, cd);

    FrustumCorners const result = _math_frustum_corners_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_frustum_corners_at_1(FSize const *const corners, FSize const split_dist, FSize const far_dist, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "corners", (void*) corners);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec4 cc[8] = DEFAULT_INITIALIZATION;
    vec4 cd[4] = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 8; i++) {
        _math_vec4_raw_to_cglm(corners + i * 4, cc[i]);
    }

    /* cglm computes split / far in FLOAT. Both values go through the bounded conversion (0.0f
     * outside float range, NaN included), then the divisor and the quotient are tested: a far
     * distance that narrows to 0, a subnormal that overflows the quotient, zero, negative or NaN is
     * refused to the zeroed corners - data, not a bug. Corners far from the origin can still
     * overflow inside cglm; this guards the divisor, not the result. */
    float const csplit = _math_fsize_to_float_bounded(split_dist);
    float const cfar = _math_fsize_to_float_bounded(far_dist);
    /* The bounded conversion turns an out-of-range, NaN or Inf split into 0.0f, which the quotient
     * test would accept as a split of 0 - so the split is refused on the F64 itself. */
    bool const split_in_range = split_dist >= -(F64) FLT_MAX && split_dist <= (F64) FLT_MAX;

    if (split_in_range && cfar >= FLT_MIN && isfinite(csplit / cfar)) {
        glmc_frustum_corners_at(cc, csplit, cfar, cd);
    }

    for (USize i = 0; i < 4; i++) {
        _math_vec4_raw_from_cglm(cd[i], dest + i * 4);
    }

    trace_log_pop();
}

FrustumSplitCorners math_frustum_corners_at_2(FrustumCorners const corners, FSize const split_dist, FSize const far_dist) {
    trace_log_push(LOG_METADATA);

    vec4 cc[8] = DEFAULT_INITIALIZATION;
    vec4 cd[4] = DEFAULT_INITIALIZATION;

    _math_frustum_corners_to_cglm(corners, cc);

    /* Bounded conversion, then the divisor and quotient tested (see math_frustum_corners_at_1). */
    float const csplit = _math_fsize_to_float_bounded(split_dist);
    float const cfar = _math_fsize_to_float_bounded(far_dist);
    /* The bounded conversion turns an out-of-range, NaN or Inf split into 0.0f, which the quotient
     * test would accept as a split of 0 - so the split is refused on the F64 itself. */
    bool const split_in_range = split_dist >= -(F64) FLT_MAX && split_dist <= (F64) FLT_MAX;

    if (split_in_range && cfar >= FLT_MIN && isfinite(csplit / cfar)) {
        glmc_frustum_corners_at(cc, csplit, cfar, cd);
    }

    FrustumSplitCorners const result = _math_frustum_split_corners_from_cglm(cd);

    trace_log_pop();

    return result;
}

void math_frustum_planes_1(FSize const *const m, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cd[6] = DEFAULT_INITIALIZATION;

    _math_mat4_raw_to_cglm(m, cm);
    glmc_frustum_planes(cm, cd);

    for (USize i = 0; i < 6; i++) {
        _math_vec4_raw_from_cglm(cd[i], dest + i * 4);
    }

    trace_log_pop();
}

FrustumPlanes math_frustum_planes_2(Mat4 const m) {
    trace_log_push(LOG_METADATA);

    mat4 cm = DEFAULT_INITIALIZATION;
    vec4 cd[6] = DEFAULT_INITIALIZATION;

    _math_mat4_to_cglm(m, cm);
    glmc_frustum_planes(cm, cd);

    FrustumPlanes const result = _math_frustum_planes_from_cglm(cd);

    trace_log_pop();

    return result;
}