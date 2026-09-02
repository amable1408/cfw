/*
 * aabb2d.c - 2D axis-aligned bounding box operations for the CFW math module.
 *
 * See aabb2d.h for API documentation and usage examples.
 */

#include <math/aabb2d.h>

/*==============================================================================
 * MARK: - Aabb2d API
 *============================================================================*/

bool math_aabb2d_aabb_1(FSize const *const aabb, FSize const *const other) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "other", (void*) other);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cother[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    _math_vec2_raw_to_cglm(other, cother[0]);
    _math_vec2_raw_to_cglm(other + 2, cother[1]);

    bool const result = glmc_aabb2d_aabb(caabb, cother);

    trace_log_pop();

    return result;
}

bool math_aabb2d_aabb_2(Aabb2d const aabb, Aabb2d const other) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cother[2] = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    _math_vec2_to_cglm(other.min, cother[0]);
    _math_vec2_to_cglm(other.max, cother[1]);

    bool const result = glmc_aabb2d_aabb(caabb, cother);

    trace_log_pop();

    return result;
}

void math_aabb2d_center_1(FSize const *const aabb, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cdest = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    glmc_aabb2d_center(caabb, cdest);
    _math_vec2_raw_from_cglm(cdest, dest);

    trace_log_pop();
}

Vec2 math_aabb2d_center_2(Aabb2d const aabb) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cdest = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    glmc_aabb2d_center(caabb, cdest);

    Vec2 const result = _math_vec2_from_cglm(cdest);

    trace_log_pop();

    return result;
}

bool math_aabb2d_circle_1(FSize const *const aabb, FSize const *const circle) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "circle", (void*) circle);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec3 cs = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    _math_vec3_raw_to_cglm(circle, cs);

    bool const result = glmc_aabb2d_circle(caabb, cs);

    trace_log_pop();

    return result;
}

bool math_aabb2d_circle_2(Aabb2d const aabb, Circle const circle) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec3 cs = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    _math_circle_to_cglm(circle, cs);

    bool const result = glmc_aabb2d_circle(caabb, cs);

    trace_log_pop();

    return result;
}

bool math_aabb2d_contains_1(FSize const *const aabb, FSize const *const other) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "other", (void*) other);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cother[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    _math_vec2_raw_to_cglm(other, cother[0]);
    _math_vec2_raw_to_cglm(other + 2, cother[1]);

    bool const result = glmc_aabb2d_contains(caabb, cother);

    trace_log_pop();

    return result;
}

bool math_aabb2d_contains_2(Aabb2d const aabb, Aabb2d const other) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cother[2] = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    _math_vec2_to_cglm(other.min, cother[0]);
    _math_vec2_to_cglm(other.max, cother[1]);

    bool const result = glmc_aabb2d_contains(caabb, cother);

    trace_log_pop();

    return result;
}

void math_aabb2d_copy_1(FSize const *const aabb, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    glmc_aabb2d_copy(caabb, cdest);
    _math_vec2_raw_from_cglm(cdest[0], dest);
    _math_vec2_raw_from_cglm(cdest[1], dest + 2);

    trace_log_pop();
}

Aabb2d math_aabb2d_copy_2(Aabb2d const aabb) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    glmc_aabb2d_copy(caabb, cdest);

    Aabb2d const result = { _math_vec2_from_cglm(cdest[0]), _math_vec2_from_cglm(cdest[1]) };

    trace_log_pop();

    return result;
}

void math_aabb2d_crop_1(FSize const *const aabb, FSize const *const crop_aabb, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "crop_aabb", (void*) crop_aabb);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 ccrop[2] = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    _math_vec2_raw_to_cglm(crop_aabb, ccrop[0]);
    _math_vec2_raw_to_cglm(crop_aabb + 2, ccrop[1]);
    glmc_aabb2d_crop(caabb, ccrop, cdest);
    _math_vec2_raw_from_cglm(cdest[0], dest);
    _math_vec2_raw_from_cglm(cdest[1], dest + 2);

    trace_log_pop();
}

Aabb2d math_aabb2d_crop_2(Aabb2d const aabb, Aabb2d const crop_aabb) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 ccrop[2] = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    _math_vec2_to_cglm(crop_aabb.min, ccrop[0]);
    _math_vec2_to_cglm(crop_aabb.max, ccrop[1]);
    glmc_aabb2d_crop(caabb, ccrop, cdest);

    Aabb2d const result = { _math_vec2_from_cglm(cdest[0]), _math_vec2_from_cglm(cdest[1]) };

    trace_log_pop();

    return result;
}

void math_aabb2d_crop_until_1(FSize const *const aabb, FSize const *const crop_aabb, FSize const *const clamp_aabb, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "crop_aabb", (void*) crop_aabb);
    error_check_null(LOG_METADATA, "clamp_aabb", (void*) clamp_aabb);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 ccrop[2] = DEFAULT_INITIALIZATION;
    vec2 cclamp[2] = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    _math_vec2_raw_to_cglm(crop_aabb, ccrop[0]);
    _math_vec2_raw_to_cglm(crop_aabb + 2, ccrop[1]);
    _math_vec2_raw_to_cglm(clamp_aabb, cclamp[0]);
    _math_vec2_raw_to_cglm(clamp_aabb + 2, cclamp[1]);
    glmc_aabb2d_crop_until(caabb, ccrop, cclamp, cdest);
    _math_vec2_raw_from_cglm(cdest[0], dest);
    _math_vec2_raw_from_cglm(cdest[1], dest + 2);

    trace_log_pop();
}

Aabb2d math_aabb2d_crop_until_2(Aabb2d const aabb, Aabb2d const crop_aabb, Aabb2d const clamp_aabb) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 ccrop[2] = DEFAULT_INITIALIZATION;
    vec2 cclamp[2] = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    _math_vec2_to_cglm(crop_aabb.min, ccrop[0]);
    _math_vec2_to_cglm(crop_aabb.max, ccrop[1]);
    _math_vec2_to_cglm(clamp_aabb.min, cclamp[0]);
    _math_vec2_to_cglm(clamp_aabb.max, cclamp[1]);
    glmc_aabb2d_crop_until(caabb, ccrop, cclamp, cdest);

    Aabb2d const result = { _math_vec2_from_cglm(cdest[0]), _math_vec2_from_cglm(cdest[1]) };

    trace_log_pop();

    return result;
}

FSize math_aabb2d_diag_1(FSize const *const aabb) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);

    FSize const result = (FSize) glmc_aabb2d_diag(caabb);

    trace_log_pop();

    return result;
}

FSize math_aabb2d_diag_2(Aabb2d const aabb) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);

    FSize const result = (FSize) glmc_aabb2d_diag(caabb);

    trace_log_pop();

    return result;
}

void math_aabb2d_from_rect_1(FSize const *const rect, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "rect", (void*) rect);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    /* Read all four before writing: dest may overlap rect, and the second pair reads the first. */
    FSize const x = rect[0];
    FSize const y = rect[1];
    FSize const w = rect[2];
    FSize const h = rect[3];

    dest[0] = x;
    dest[1] = y;
    dest[2] = x + w;
    dest[3] = y + h;

    trace_log_pop();
}

Aabb2d math_aabb2d_from_rect_2(Rect const rect) {
    trace_log_push(LOG_METADATA);

    Aabb2d const result = { { rect.x, rect.y }, { rect.x + rect.w, rect.y + rect.h } };

    trace_log_pop();

    return result;
}

void math_aabb2d_invalidate_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    glmc_aabb2d_invalidate(caabb);
    _math_vec2_raw_from_cglm(caabb[0], dest);
    _math_vec2_raw_from_cglm(caabb[1], dest + 2);

    trace_log_pop();
}

Aabb2d math_aabb2d_invalidate_2(void) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    glmc_aabb2d_invalidate(caabb);

    Aabb2d const result = { _math_vec2_from_cglm(caabb[0]), _math_vec2_from_cglm(caabb[1]) };

    trace_log_pop();

    return result;
}

bool math_aabb2d_isvalid_1(FSize const *const aabb) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);

    bool const result = glmc_aabb2d_isvalid(caabb);

    trace_log_pop();

    return result;
}

bool math_aabb2d_isvalid_2(Aabb2d const aabb) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);

    bool const result = glmc_aabb2d_isvalid(caabb);

    trace_log_pop();

    return result;
}

void math_aabb2d_merge_1(FSize const *const aabb1, FSize const *const aabb2, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb1", (void*) aabb1);
    error_check_null(LOG_METADATA, "aabb2", (void*) aabb2);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 ca[2] = DEFAULT_INITIALIZATION;
    vec2 cb[2] = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb1, ca[0]);
    _math_vec2_raw_to_cglm(aabb1 + 2, ca[1]);
    _math_vec2_raw_to_cglm(aabb2, cb[0]);
    _math_vec2_raw_to_cglm(aabb2 + 2, cb[1]);
    glmc_aabb2d_merge(ca, cb, cdest);
    _math_vec2_raw_from_cglm(cdest[0], dest);
    _math_vec2_raw_from_cglm(cdest[1], dest + 2);

    trace_log_pop();
}

Aabb2d math_aabb2d_merge_2(Aabb2d const aabb1, Aabb2d const aabb2) {
    trace_log_push(LOG_METADATA);

    vec2 ca[2] = DEFAULT_INITIALIZATION;
    vec2 cb[2] = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_vec2_to_cglm(aabb1.min, ca[0]);
    _math_vec2_to_cglm(aabb1.max, ca[1]);
    _math_vec2_to_cglm(aabb2.min, cb[0]);
    _math_vec2_to_cglm(aabb2.max, cb[1]);
    glmc_aabb2d_merge(ca, cb, cdest);

    Aabb2d const result = { _math_vec2_from_cglm(cdest[0]), _math_vec2_from_cglm(cdest[1]) };

    trace_log_pop();

    return result;
}

bool math_aabb2d_point_1(FSize const *const aabb, FSize const *const point) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "point", (void*) point);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cpoint = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    _math_vec2_raw_to_cglm(point, cpoint);

    bool const result = glmc_aabb2d_point(caabb, cpoint);

    trace_log_pop();

    return result;
}

bool math_aabb2d_point_2(Aabb2d const aabb, Vec2 const point) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cpoint = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    _math_vec2_to_cglm(point, cpoint);

    bool const result = glmc_aabb2d_point(caabb, cpoint);

    trace_log_pop();

    return result;
}

FSize math_aabb2d_radius_1(FSize const *const aabb) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);

    FSize const result = (FSize) glmc_aabb2d_radius(caabb);

    trace_log_pop();

    return result;
}

FSize math_aabb2d_radius_2(Aabb2d const aabb) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);

    FSize const result = (FSize) glmc_aabb2d_radius(caabb);

    trace_log_pop();

    return result;
}

void math_aabb2d_sizev_1(FSize const *const aabb, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cdest = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    glmc_aabb2d_sizev(caabb, cdest);
    _math_vec2_raw_from_cglm(cdest, dest);

    trace_log_pop();
}

Vec2 math_aabb2d_sizev_2(Aabb2d const aabb) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    vec2 cdest = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    glmc_aabb2d_sizev(caabb, cdest);

    Vec2 const result = _math_vec2_from_cglm(cdest);

    trace_log_pop();

    return result;
}

void math_aabb2d_transform_1(FSize const *const aabb, FSize const *const m, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "aabb", (void*) aabb);
    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    mat3 cm = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_vec2_raw_to_cglm(aabb, caabb[0]);
    _math_vec2_raw_to_cglm(aabb + 2, caabb[1]);
    _math_mat3_raw_to_cglm(m, cm);
    glmc_aabb2d_transform(caabb, cm, cdest);
    _math_vec2_raw_from_cglm(cdest[0], dest);
    _math_vec2_raw_from_cglm(cdest[1], dest + 2);

    trace_log_pop();
}

Aabb2d math_aabb2d_transform_2(Aabb2d const aabb, Mat3 const m) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;
    mat3 cm = DEFAULT_INITIALIZATION;
    vec2 cdest[2] = DEFAULT_INITIALIZATION;

    _math_aabb2d_to_cglm(aabb, caabb);
    _math_mat3_to_cglm(m, cm);
    glmc_aabb2d_transform(caabb, cm, cdest);

    Aabb2d const result = { _math_vec2_from_cglm(cdest[0]), _math_vec2_from_cglm(cdest[1]) };

    trace_log_pop();

    return result;
}

void math_aabb2d_zero_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    glmc_aabb2d_zero(caabb);
    _math_vec2_raw_from_cglm(caabb[0], dest);
    _math_vec2_raw_from_cglm(caabb[1], dest + 2);

    trace_log_pop();
}

Aabb2d math_aabb2d_zero_2(void) {
    trace_log_push(LOG_METADATA);

    vec2 caabb[2] = DEFAULT_INITIALIZATION;

    glmc_aabb2d_zero(caabb);

    Aabb2d const result = { _math_vec2_from_cglm(caabb[0]), _math_vec2_from_cglm(caabb[1]) };

    trace_log_pop();

    return result;
}