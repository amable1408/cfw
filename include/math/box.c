/*
 * box.c - 3D axis-aligned bounding box (AABB) operations for the CFW math module.
 *
 * See box.h for API documentation and usage examples.
 */

#include <math/box.h>

/*==============================================================================
 * MARK: - Box API
 *============================================================================*/

bool math_box_aabb_1(FSize const *const box, FSize const *const other) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "other", (void*) other);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 cother[2] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    _math_vec3_raw_to_cglm(other, cother[0]);
    _math_vec3_raw_to_cglm(other + 3, cother[1]);

    bool const result = glmc_aabb_aabb(cbox, cother);

    trace_log_pop();

    return result;
}

bool math_box_aabb_2(Box const box, Box const other) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 cother[2] = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    _math_vec3_to_cglm(other.min, cother[0]);
    _math_vec3_to_cglm(other.max, cother[1]);

    bool const result = glmc_aabb_aabb(cbox, cother);

    trace_log_pop();

    return result;
}

void math_box_center_1(FSize const *const box, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    glmc_aabb_center(cbox, cd);
    _math_vec3_raw_from_cglm(cd, dest);

    trace_log_pop();
}

Vec3 math_box_center_2(Box const box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 cd = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    glmc_aabb_center(cbox, cd);

    Vec3 const result = _math_vec3_from_cglm(cd);

    trace_log_pop();

    return result;
}

bool math_box_contains_1(FSize const *const box, FSize const *const other) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "other", (void*) other);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 cother[2] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    _math_vec3_raw_to_cglm(other, cother[0]);
    _math_vec3_raw_to_cglm(other + 3, cother[1]);

    bool const result = glmc_aabb_contains(cbox, cother);

    trace_log_pop();

    return result;
}

bool math_box_contains_2(Box const box, Box const other) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 cother[2] = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    _math_vec3_to_cglm(other.min, cother[0]);
    _math_vec3_to_cglm(other.max, cother[1]);

    bool const result = glmc_aabb_contains(cbox, cother);

    trace_log_pop();

    return result;
}

void math_box_crop_1(FSize const *const box, FSize const *const crop_box, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "crop_box", (void*) crop_box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 ccrop[2] = DEFAULT_INITIALIZATION;
    vec3 cd[2] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    _math_vec3_raw_to_cglm(crop_box, ccrop[0]);
    _math_vec3_raw_to_cglm(crop_box + 3, ccrop[1]);
    glmc_aabb_crop(cbox, ccrop, cd);
    _math_vec3_raw_from_cglm(cd[0], dest);
    _math_vec3_raw_from_cglm(cd[1], dest + 3);

    trace_log_pop();
}

Box math_box_crop_2(Box const box, Box const crop_box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 ccrop[2] = DEFAULT_INITIALIZATION;
    vec3 cd[2] = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    _math_vec3_to_cglm(crop_box.min, ccrop[0]);
    _math_vec3_to_cglm(crop_box.max, ccrop[1]);
    glmc_aabb_crop(cbox, ccrop, cd);

    Box const result = { _math_vec3_from_cglm(cd[0]), _math_vec3_from_cglm(cd[1]) };

    trace_log_pop();

    return result;
}

void math_box_crop_until_1(FSize const *const box, FSize const *const crop_box, FSize const *const clamp_box, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "crop_box", (void*) crop_box);
    error_check_null(LOG_METADATA, "clamp_box", (void*) clamp_box);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 ccrop[2] = DEFAULT_INITIALIZATION;
    vec3 cclamp[2] = DEFAULT_INITIALIZATION;
    vec3 cd[2] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    _math_vec3_raw_to_cglm(crop_box, ccrop[0]);
    _math_vec3_raw_to_cglm(crop_box + 3, ccrop[1]);
    _math_vec3_raw_to_cglm(clamp_box, cclamp[0]);
    _math_vec3_raw_to_cglm(clamp_box + 3, cclamp[1]);
    glmc_aabb_crop_until(cbox, ccrop, cclamp, cd);
    _math_vec3_raw_from_cglm(cd[0], dest);
    _math_vec3_raw_from_cglm(cd[1], dest + 3);

    trace_log_pop();
}

Box math_box_crop_until_2(Box const box, Box const crop_box, Box const clamp_box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 ccrop[2] = DEFAULT_INITIALIZATION;
    vec3 cclamp[2] = DEFAULT_INITIALIZATION;
    vec3 cd[2] = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    _math_vec3_to_cglm(crop_box.min, ccrop[0]);
    _math_vec3_to_cglm(crop_box.max, ccrop[1]);
    _math_vec3_to_cglm(clamp_box.min, cclamp[0]);
    _math_vec3_to_cglm(clamp_box.max, cclamp[1]);
    glmc_aabb_crop_until(cbox, ccrop, cclamp, cd);

    Box const result = { _math_vec3_from_cglm(cd[0]), _math_vec3_from_cglm(cd[1]) };

    trace_log_pop();

    return result;
}

bool math_box_frustum_1(FSize const *const box, FSize const *const planes) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "planes", (void*) planes);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec4 cplanes[6] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);

    for (USize i = 0; i < 6; i++) {
        _math_vec4_raw_to_cglm(planes + i * 4, cplanes[i]);
    }

    bool const result = glmc_aabb_frustum(cbox, cplanes);

    trace_log_pop();

    return result;
}

bool math_box_frustum_2(Box const box, FrustumPlanes const planes) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec4 cplanes[6] = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    _math_frustum_planes_to_cglm(planes, cplanes);

    bool const result = glmc_aabb_frustum(cbox, cplanes);

    trace_log_pop();

    return result;
}

void math_box_invalidate_1(FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    glmc_aabb_invalidate(cbox);
    _math_vec3_raw_from_cglm(cbox[0], dest);
    _math_vec3_raw_from_cglm(cbox[1], dest + 3);

    trace_log_pop();
}

Box math_box_invalidate_2(void) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    glmc_aabb_invalidate(cbox);

    Box const result = { _math_vec3_from_cglm(cbox[0]), _math_vec3_from_cglm(cbox[1]) };

    trace_log_pop();

    return result;
}

bool math_box_isvalid_1(FSize const *const box) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);

    bool const result = glmc_aabb_isvalid(cbox);

    trace_log_pop();

    return result;
}

bool math_box_isvalid_2(Box const box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);

    bool const result = glmc_aabb_isvalid(cbox);

    trace_log_pop();

    return result;
}

void math_box_merge_1(FSize const *const box1, FSize const *const box2, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box1", (void*) box1);
    error_check_null(LOG_METADATA, "box2", (void*) box2);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox1[2] = DEFAULT_INITIALIZATION;
    vec3 cbox2[2] = DEFAULT_INITIALIZATION;
    vec3 cd[2] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box1, cbox1[0]);
    _math_vec3_raw_to_cglm(box1 + 3, cbox1[1]);
    _math_vec3_raw_to_cglm(box2, cbox2[0]);
    _math_vec3_raw_to_cglm(box2 + 3, cbox2[1]);
    glmc_aabb_merge(cbox1, cbox2, cd);
    _math_vec3_raw_from_cglm(cd[0], dest);
    _math_vec3_raw_from_cglm(cd[1], dest + 3);

    trace_log_pop();
}

Box math_box_merge_2(Box const box1, Box const box2) {
    trace_log_push(LOG_METADATA);

    vec3 cbox1[2] = DEFAULT_INITIALIZATION;
    vec3 cbox2[2] = DEFAULT_INITIALIZATION;
    vec3 cd[2] = DEFAULT_INITIALIZATION;

    _math_vec3_to_cglm(box1.min, cbox1[0]);
    _math_vec3_to_cglm(box1.max, cbox1[1]);
    _math_vec3_to_cglm(box2.min, cbox2[0]);
    _math_vec3_to_cglm(box2.max, cbox2[1]);
    glmc_aabb_merge(cbox1, cbox2, cd);

    Box const result = { _math_vec3_from_cglm(cd[0]), _math_vec3_from_cglm(cd[1]) };

    trace_log_pop();

    return result;
}

bool math_box_point_1(FSize const *const box, FSize const *const point) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "point", (void*) point);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 cpoint = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    _math_vec3_raw_to_cglm(point, cpoint);

    bool const result = glmc_aabb_point(cbox, cpoint);

    trace_log_pop();

    return result;
}

bool math_box_point_2(Box const box, Vec3 const point) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec3 cpoint = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    _math_vec3_to_cglm(point, cpoint);

    bool const result = glmc_aabb_point(cbox, cpoint);

    trace_log_pop();

    return result;
}

FSize math_box_radius_1(FSize const *const box) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);

    FSize const result = (FSize) glmc_aabb_radius(cbox);

    trace_log_pop();

    return result;
}

FSize math_box_radius_2(Box const box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);

    FSize const result = (FSize) glmc_aabb_radius(cbox);

    trace_log_pop();

    return result;
}

FSize math_box_size_1(FSize const *const box) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);

    FSize const result = (FSize) glmc_aabb_size(cbox);

    trace_log_pop();

    return result;
}

FSize math_box_size_2(Box const box) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);

    FSize const result = (FSize) glmc_aabb_size(cbox);

    trace_log_pop();

    return result;
}

bool math_box_sphere_1(FSize const *const box, FSize const *const sphere) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "sphere", (void*) sphere);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec4 cs = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    _math_vec4_raw_to_cglm(sphere, cs);

    bool const result = glmc_aabb_sphere(cbox, cs);

    trace_log_pop();

    return result;
}

bool math_box_sphere_2(Box const box, Sphere const sphere) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    vec4 cs = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    _math_sphere_to_cglm(sphere, cs);

    bool const result = glmc_aabb_sphere(cbox, cs);

    trace_log_pop();

    return result;
}

void math_box_transform_1(FSize const *const box, FSize const *const m, FSize *const dest) {
    trace_log_push(LOG_METADATA);

    error_check_null(LOG_METADATA, "box", (void*) box);
    error_check_null(LOG_METADATA, "m", (void*) m);
    error_check_null(LOG_METADATA, "dest", (void*) dest);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cd[2] = DEFAULT_INITIALIZATION;

    _math_vec3_raw_to_cglm(box, cbox[0]);
    _math_vec3_raw_to_cglm(box + 3, cbox[1]);
    _math_mat4_raw_to_cglm(m, cm);
    glmc_aabb_transform(cbox, cm, cd);
    _math_vec3_raw_from_cglm(cd[0], dest);
    _math_vec3_raw_from_cglm(cd[1], dest + 3);

    trace_log_pop();
}

Box math_box_transform_2(Box const box, Mat4 const m) {
    trace_log_push(LOG_METADATA);

    vec3 cbox[2] = DEFAULT_INITIALIZATION;
    mat4 cm = DEFAULT_INITIALIZATION;
    vec3 cd[2] = DEFAULT_INITIALIZATION;

    _math_box_to_cglm(box, cbox);
    _math_mat4_to_cglm(m, cm);
    glmc_aabb_transform(cbox, cm, cd);

    Box const result = { _math_vec3_from_cglm(cd[0]), _math_vec3_from_cglm(cd[1]) };

    trace_log_pop();

    return result;
}