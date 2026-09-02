/*
 * clipspace.h - Clipspace projection/view variants for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_* clipspace API: orthographic,
 *     frustum, and perspective projection builders, AABB-fit orthographic
 *     builders, perspective resize / far-plane moves, full and partial frustum
 *     decomposition, projection sizes/fovy/aspect queries, look-at / look view
 *     builders, and project / unproject / project-Z, each across every
 *     handedness (lh/rh) and clip-depth (NO [-1,1] / ZO [0,1]) variant
 *   - Camera functions: same math_cam_ prefix and conceptual module as cam core,
 *     split into this file to parallelize; only the handedness/depth VARIANTS
 *     live here (the un-suffixed perspective/lookat/ortho stay in cam core)
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct
 *     superset; multi-output frustum decompositions expose only the _1
 *     out-pointer form (no single struct fits their several scalar outputs)
 *   - project / unprojecti / project_z appear here under the math_cam_ prefix in their
 *     _zo forms; their unsuffixed (NO) twins are project.h's math_project_*
 *
 * Usage Examples:
 *   @code
 *   Mat4 const proj = math_cam_perspective_rh_no_2(1.0472, 1.777, 0.1, 100.0);
 *   Mat4 const view = math_cam_lookat_rh_no_2(eye, center, up);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - Degenerate input propagates Inf/NaN, except the eight perspective_resize variants:
 *     when proj[0][0] == 0 they return proj unchanged - a finite refusal.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper converts FSize<->float at the boundary and calls a compiled
 *     glmc_* routine. cglm's clipspace routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat4/Vec3/Vec4 types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * See clipspace.c for implementation details.
 */

#ifndef MATH_CLIPSPACE_H
#define MATH_CLIPSPACE_H

/*
 * cglm's <cglm/call.h> aggregate (pulled in by <math/types.h>) does not declare
 * the compiled clipspace projection/view variants, so their glmc_* prototypes
 * are included directly here; the matching symbols live in the system libcglm (0.9.6).
 */
#include <cglm/call/clipspace/ortho_lh_no.h>
#include <cglm/call/clipspace/ortho_lh_zo.h>
#include <cglm/call/clipspace/ortho_rh_no.h>
#include <cglm/call/clipspace/ortho_rh_zo.h>
#include <cglm/call/clipspace/persp_lh_no.h>
#include <cglm/call/clipspace/persp_lh_zo.h>
#include <cglm/call/clipspace/persp_rh_no.h>
#include <cglm/call/clipspace/persp_rh_zo.h>
#include <cglm/call/clipspace/project_no.h>
#include <cglm/call/clipspace/project_zo.h>
#include <cglm/call/clipspace/view_lh_no.h>
#include <cglm/call/clipspace/view_lh_zo.h>
#include <cglm/call/clipspace/view_rh_no.h>
#include <cglm/call/clipspace/view_rh_zo.h>

#include <math/cglm_compat.h>
#include <math/types.h>

/*==============================================================================
 * MARK: - Clipspace camera API
 *
 * Raw (_1) variants read/write contiguous FSize (16 column-major for a mat4, 3
 * for a vec3, 4 for a vec4); struct (_2) variants read/return Mat4/Vec3/Vec4.
 * Handedness is lh/rh; clip depth is NO ([-1, 1]) or ZO ([0, 1]). Multi-output
 * frustum decompositions provide only the _1 out-pointer form.
 *============================================================================*/

/**
 * @brief Build a perspective projection matrix from frustum bounds (left-handed, [-1, 1] (NO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_frustum_lh_no_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Build a perspective projection matrix from frustum bounds (left-handed, [-1, 1] (NO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_frustum_lh_no_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Build a perspective projection matrix from frustum bounds (left-handed, [0, 1] (ZO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_frustum_lh_zo_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Build a perspective projection matrix from frustum bounds (left-handed, [0, 1] (ZO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_frustum_lh_zo_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Build a perspective projection matrix from frustum bounds (right-handed, [-1, 1] (NO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_frustum_rh_no_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Build a perspective projection matrix from frustum bounds (right-handed, [-1, 1] (NO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_frustum_rh_no_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Build a perspective projection matrix from frustum bounds (right-handed, [0, 1] (ZO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_frustum_rh_zo_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Build a perspective projection matrix from frustum bounds (right-handed, [0, 1] (ZO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_frustum_rh_zo_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Build a view matrix from eye and direction with an arbitrary up vector (left-handed, [-1, 1] (NO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw view direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_anyup_lh_no_1(FSize const *const eye, FSize const *const dir, FSize *const dest);

/**
 * @brief Build a view matrix from eye and direction with an arbitrary up vector (left-handed, [-1, 1] (NO) depth).
 * @param eye Eye position vector.
 * @param dir View direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_look_anyup_lh_no_2(Vec3 const eye, Vec3 const dir);

/**
 * @brief Build a view matrix from eye and direction with an arbitrary up vector (left-handed, [0, 1] (ZO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw view direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_anyup_lh_zo_1(FSize const *const eye, FSize const *const dir, FSize *const dest);

/**
 * @brief Build a view matrix from eye and direction with an arbitrary up vector (left-handed, [0, 1] (ZO) depth).
 * @param eye Eye position vector.
 * @param dir View direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_look_anyup_lh_zo_2(Vec3 const eye, Vec3 const dir);

/**
 * @brief Build a view matrix from eye and direction with an arbitrary up vector (right-handed, [-1, 1] (NO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw view direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_anyup_rh_no_1(FSize const *const eye, FSize const *const dir, FSize *const dest);

/**
 * @brief Build a view matrix from eye and direction with an arbitrary up vector (right-handed, [-1, 1] (NO) depth).
 * @param eye Eye position vector.
 * @param dir View direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_look_anyup_rh_no_2(Vec3 const eye, Vec3 const dir);

/**
 * @brief Build a view matrix from eye and direction with an arbitrary up vector (right-handed, [0, 1] (ZO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw view direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_anyup_rh_zo_1(FSize const *const eye, FSize const *const dir, FSize *const dest);

/**
 * @brief Build a view matrix from eye and direction with an arbitrary up vector (right-handed, [0, 1] (ZO) depth).
 * @param eye Eye position vector.
 * @param dir View direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_look_anyup_rh_zo_2(Vec3 const eye, Vec3 const dir);

/**
 * @brief Build a view matrix from eye, direction, and up vectors (left-handed, [-1, 1] (NO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw view direction (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_lh_no_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest);

/**
 * @brief Build a view matrix from eye, direction, and up vectors (left-handed, [-1, 1] (NO) depth).
 * @param eye Eye position vector.
 * @param dir View direction vector.
 * @param up Up direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_look_lh_no_2(Vec3 const eye, Vec3 const dir, Vec3 const up);

/**
 * @brief Build a view matrix from eye, direction, and up vectors (left-handed, [0, 1] (ZO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw view direction (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_lh_zo_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest);

/**
 * @brief Build a view matrix from eye, direction, and up vectors (left-handed, [0, 1] (ZO) depth).
 * @param eye Eye position vector.
 * @param dir View direction vector.
 * @param up Up direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_look_lh_zo_2(Vec3 const eye, Vec3 const dir, Vec3 const up);

/**
 * @brief Build a view matrix from eye, direction, and up vectors (right-handed, [-1, 1] (NO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw view direction (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_rh_no_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest);

/**
 * @brief Build a view matrix from eye, direction, and up vectors (right-handed, [-1, 1] (NO) depth).
 * @param eye Eye position vector.
 * @param dir View direction vector.
 * @param up Up direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_look_rh_no_2(Vec3 const eye, Vec3 const dir, Vec3 const up);

/**
 * @brief Build a view matrix from eye, direction, and up vectors (right-handed, [0, 1] (ZO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw view direction (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_rh_zo_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest);

/**
 * @brief Build a view matrix from eye, direction, and up vectors (right-handed, [0, 1] (ZO) depth).
 * @param eye Eye position vector.
 * @param dir View direction vector.
 * @param up Up direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_look_rh_zo_2(Vec3 const eye, Vec3 const dir, Vec3 const up);

/**
 * @brief Build a view matrix from eye, center, and up vectors (left-handed, [-1, 1] (NO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param center Raw target center (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_lookat_lh_no_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest);

/**
 * @brief Build a view matrix from eye, center, and up vectors (left-handed, [-1, 1] (NO) depth).
 * @param eye Eye position vector.
 * @param center Target center vector.
 * @param up Up direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_lookat_lh_no_2(Vec3 const eye, Vec3 const center, Vec3 const up);

/**
 * @brief Build a view matrix from eye, center, and up vectors (left-handed, [0, 1] (ZO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param center Raw target center (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_lookat_lh_zo_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest);

/**
 * @brief Build a view matrix from eye, center, and up vectors (left-handed, [0, 1] (ZO) depth).
 * @param eye Eye position vector.
 * @param center Target center vector.
 * @param up Up direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_lookat_lh_zo_2(Vec3 const eye, Vec3 const center, Vec3 const up);

/**
 * @brief Build a view matrix from eye, center, and up vectors (right-handed, [-1, 1] (NO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param center Raw target center (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_lookat_rh_no_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest);

/**
 * @brief Build a view matrix from eye, center, and up vectors (right-handed, [-1, 1] (NO) depth).
 * @param eye Eye position vector.
 * @param center Target center vector.
 * @param up Up direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_lookat_rh_no_2(Vec3 const eye, Vec3 const center, Vec3 const up);

/**
 * @brief Build a view matrix from eye, center, and up vectors (right-handed, [0, 1] (ZO) depth).
 * @param eye Raw eye position (3 contiguous FSize).
 * @param center Raw target center (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_lookat_rh_zo_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest);

/**
 * @brief Build a view matrix from eye, center, and up vectors (right-handed, [0, 1] (ZO) depth).
 * @param eye Eye position vector.
 * @param center Target center vector.
 * @param up Up direction vector.
 * @return Resulting Mat4.
 */
Mat4 math_cam_lookat_rh_zo_2(Vec3 const eye, Vec3 const center, Vec3 const up);

/**
 * @brief Build an orthographic projection matrix that fits an axis-aligned bounding box (left-handed, [-1, 1] (NO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_lh_no_1(FSize const *const box, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix that fits an axis-aligned bounding box (left-handed, [-1, 1] (NO) depth).
 * @param box Bounding box (min and max corners).
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_lh_no_2(Box const box);

/**
 * @brief Build an orthographic projection matrix that fits an axis-aligned bounding box (left-handed, [0, 1] (ZO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_lh_zo_1(FSize const *const box, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix that fits an axis-aligned bounding box (left-handed, [0, 1] (ZO) depth).
 * @param box Bounding box (min and max corners).
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_lh_zo_2(Box const box);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with uniform padding (left-handed, [-1, 1] (NO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param padding Padding applied to the fitted box.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_p_lh_no_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with uniform padding (left-handed, [-1, 1] (NO) depth).
 * @param box Bounding box (min and max corners).
 * @param padding Padding applied to the fitted box.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_p_lh_no_2(Box const box, FSize const padding);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with uniform padding (left-handed, [0, 1] (ZO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param padding Padding applied to the fitted box.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_p_lh_zo_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with uniform padding (left-handed, [0, 1] (ZO) depth).
 * @param box Bounding box (min and max corners).
 * @param padding Padding applied to the fitted box.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_p_lh_zo_2(Box const box, FSize const padding);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with uniform padding (right-handed, [-1, 1] (NO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param padding Padding applied to the fitted box.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_p_rh_no_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with uniform padding (right-handed, [-1, 1] (NO) depth).
 * @param box Bounding box (min and max corners).
 * @param padding Padding applied to the fitted box.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_p_rh_no_2(Box const box, FSize const padding);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with uniform padding (right-handed, [0, 1] (ZO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param padding Padding applied to the fitted box.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_p_rh_zo_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with uniform padding (right-handed, [0, 1] (ZO) depth).
 * @param box Bounding box (min and max corners).
 * @param padding Padding applied to the fitted box.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_p_rh_zo_2(Box const box, FSize const padding);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with padding on the Z axis (left-handed, [-1, 1] (NO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param padding Padding applied to the fitted box.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_pz_lh_no_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with padding on the Z axis (left-handed, [-1, 1] (NO) depth).
 * @param box Bounding box (min and max corners).
 * @param padding Padding applied to the fitted box.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_pz_lh_no_2(Box const box, FSize const padding);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with padding on the Z axis (left-handed, [0, 1] (ZO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param padding Padding applied to the fitted box.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_pz_lh_zo_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with padding on the Z axis (left-handed, [0, 1] (ZO) depth).
 * @param box Bounding box (min and max corners).
 * @param padding Padding applied to the fitted box.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_pz_lh_zo_2(Box const box, FSize const padding);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with padding on the Z axis (right-handed, [-1, 1] (NO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param padding Padding applied to the fitted box.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_pz_rh_no_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with padding on the Z axis (right-handed, [-1, 1] (NO) depth).
 * @param box Bounding box (min and max corners).
 * @param padding Padding applied to the fitted box.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_pz_rh_no_2(Box const box, FSize const padding);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with padding on the Z axis (right-handed, [0, 1] (ZO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param padding Padding applied to the fitted box.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_pz_rh_zo_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix fitting an AABB with padding on the Z axis (right-handed, [0, 1] (ZO) depth).
 * @param box Bounding box (min and max corners).
 * @param padding Padding applied to the fitted box.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_pz_rh_zo_2(Box const box, FSize const padding);

/**
 * @brief Build an orthographic projection matrix that fits an axis-aligned bounding box (right-handed, [-1, 1] (NO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_rh_no_1(FSize const *const box, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix that fits an axis-aligned bounding box (right-handed, [-1, 1] (NO) depth).
 * @param box Bounding box (min and max corners).
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_rh_no_2(Box const box);

/**
 * @brief Build an orthographic projection matrix that fits an axis-aligned bounding box (right-handed, [0, 1] (ZO) depth).
 * @param box Raw AABB as 6 contiguous FSize (min xyz then max xyz).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_rh_zo_1(FSize const *const box, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix that fits an axis-aligned bounding box (right-handed, [0, 1] (ZO) depth).
 * @param box Bounding box (min and max corners).
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_aabb_rh_zo_2(Box const box);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio (left-handed, [-1, 1] (NO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_lh_no_1(FSize const aspect, FSize *const dest);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio (left-handed, [-1, 1] (NO) depth).
 * @param aspect Aspect ratio (width / height).
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_default_lh_no_2(FSize const aspect);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio (left-handed, [0, 1] (ZO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_lh_zo_1(FSize const aspect, FSize *const dest);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio (left-handed, [0, 1] (ZO) depth).
 * @param aspect Aspect ratio (width / height).
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_default_lh_zo_2(FSize const aspect);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio (right-handed, [-1, 1] (NO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_rh_no_1(FSize const aspect, FSize *const dest);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio (right-handed, [-1, 1] (NO) depth).
 * @param aspect Aspect ratio (width / height).
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_default_rh_no_2(FSize const aspect);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio (right-handed, [0, 1] (ZO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_rh_zo_1(FSize const aspect, FSize *const dest);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio (right-handed, [0, 1] (ZO) depth).
 * @param aspect Aspect ratio (width / height).
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_default_rh_zo_2(FSize const aspect);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio and size (left-handed, [-1, 1] (NO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param size Half-extent of the view volume.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_s_lh_no_1(FSize const aspect, FSize const size, FSize *const dest);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio and size (left-handed, [-1, 1] (NO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param size Half-extent of the view volume.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_default_s_lh_no_2(FSize const aspect, FSize const size);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio and size (left-handed, [0, 1] (ZO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param size Half-extent of the view volume.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_s_lh_zo_1(FSize const aspect, FSize const size, FSize *const dest);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio and size (left-handed, [0, 1] (ZO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param size Half-extent of the view volume.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_default_s_lh_zo_2(FSize const aspect, FSize const size);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio and size (right-handed, [-1, 1] (NO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param size Half-extent of the view volume.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_s_rh_no_1(FSize const aspect, FSize const size, FSize *const dest);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio and size (right-handed, [-1, 1] (NO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param size Half-extent of the view volume.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_default_s_rh_no_2(FSize const aspect, FSize const size);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio and size (right-handed, [0, 1] (ZO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param size Half-extent of the view volume.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_s_rh_zo_1(FSize const aspect, FSize const size, FSize *const dest);

/**
 * @brief Build a default orthographic projection matrix for an aspect ratio and size (right-handed, [0, 1] (ZO) depth).
 * @param aspect Aspect ratio (width / height).
 * @param size Half-extent of the view volume.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_default_s_rh_zo_2(FSize const aspect, FSize const size);

/**
 * @brief Build an orthographic projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_lh_no_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_lh_no_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Build an orthographic projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_lh_zo_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_lh_zo_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Build an orthographic projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_rh_no_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_rh_no_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Build an orthographic projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_rh_zo_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Build an orthographic projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param left Left clipping plane.
 * @param right Right clipping plane.
 * @param bottom Bottom clipping plane.
 * @param top Top clipping plane.
 * @param near_z Near clipping plane distance.
 * @param far_z Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_ortho_rh_zo_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Extract the aspect ratio from a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_aspect_lh_no_1(FSize const *const proj);

/**
 * @brief Extract the aspect ratio from a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_aspect_lh_no_2(Mat4 const proj);

/**
 * @brief Extract the aspect ratio from a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_aspect_lh_zo_1(FSize const *const proj);

/**
 * @brief Extract the aspect ratio from a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_aspect_lh_zo_2(Mat4 const proj);

/**
 * @brief Extract the aspect ratio from a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_aspect_rh_no_1(FSize const *const proj);

/**
 * @brief Extract the aspect ratio from a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_aspect_rh_no_2(Mat4 const proj);

/**
 * @brief Extract the aspect ratio from a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_aspect_rh_zo_1(FSize const *const proj);

/**
 * @brief Extract the aspect ratio from a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_aspect_rh_zo_2(Mat4 const proj);

/**
 * @brief Extract the far clipping plane distance from a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_far_lh_no_1(FSize const *const proj);

/**
 * @brief Extract the far clipping plane distance from a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_far_lh_no_2(Mat4 const proj);

/**
 * @brief Extract the far clipping plane distance from a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_far_lh_zo_1(FSize const *const proj);

/**
 * @brief Extract the far clipping plane distance from a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_far_lh_zo_2(Mat4 const proj);

/**
 * @brief Extract the far clipping plane distance from a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_far_rh_no_1(FSize const *const proj);

/**
 * @brief Extract the far clipping plane distance from a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_far_rh_no_2(Mat4 const proj);

/**
 * @brief Extract the far clipping plane distance from a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_far_rh_zo_1(FSize const *const proj);

/**
 * @brief Extract the far clipping plane distance from a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_far_rh_zo_2(Mat4 const proj);

/**
 * @brief Decompose a perspective projection matrix into its six frustum planes (left-handed, [-1, 1] (NO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param near_z Destination for the near z frustum plane value.
 * @param far_z Destination for the far z frustum plane value.
 * @param top Destination for the top frustum plane value.
 * @param bottom Destination for the bottom frustum plane value.
 * @param left Destination for the left frustum plane value.
 * @param right Destination for the right frustum plane value.
 */
void math_cam_persp_decomp_lh_no_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right);

/**
 * @brief Decompose a perspective projection matrix into its six frustum planes (left-handed, [0, 1] (ZO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param near_z Destination for the near z frustum plane value.
 * @param far_z Destination for the far z frustum plane value.
 * @param top Destination for the top frustum plane value.
 * @param bottom Destination for the bottom frustum plane value.
 * @param left Destination for the left frustum plane value.
 * @param right Destination for the right frustum plane value.
 */
void math_cam_persp_decomp_lh_zo_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right);

/**
 * @brief Extract the near clipping plane distance from a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_near_lh_no_1(FSize const *const proj);

/**
 * @brief Extract the near clipping plane distance from a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_near_lh_no_2(Mat4 const proj);

/**
 * @brief Extract the near clipping plane distance from a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_near_lh_zo_1(FSize const *const proj);

/**
 * @brief Extract the near clipping plane distance from a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_near_lh_zo_2(Mat4 const proj);

/**
 * @brief Extract the near clipping plane distance from a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_near_rh_no_1(FSize const *const proj);

/**
 * @brief Extract the near clipping plane distance from a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_near_rh_no_2(Mat4 const proj);

/**
 * @brief Extract the near clipping plane distance from a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_near_rh_zo_1(FSize const *const proj);

/**
 * @brief Extract the near clipping plane distance from a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_decomp_near_rh_zo_2(Mat4 const proj);

/**
 * @brief Decompose a perspective projection matrix into its six frustum planes (right-handed, [-1, 1] (NO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param near_z Destination for the near z frustum plane value.
 * @param far_z Destination for the far z frustum plane value.
 * @param top Destination for the top frustum plane value.
 * @param bottom Destination for the bottom frustum plane value.
 * @param left Destination for the left frustum plane value.
 * @param right Destination for the right frustum plane value.
 */
void math_cam_persp_decomp_rh_no_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right);

/**
 * @brief Decompose a perspective projection matrix into its six frustum planes (right-handed, [0, 1] (ZO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param near_z Destination for the near z frustum plane value.
 * @param far_z Destination for the far z frustum plane value.
 * @param top Destination for the top frustum plane value.
 * @param bottom Destination for the bottom frustum plane value.
 * @param left Destination for the left frustum plane value.
 * @param right Destination for the right frustum plane value.
 */
void math_cam_persp_decomp_rh_zo_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right);

/**
 * @brief Decompose the left and right frustum planes of a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 *        Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param left Destination for the left frustum plane value.
 * @param right Destination for the right frustum plane value.
 */
void math_cam_persp_decomp_x_lh_no_1(FSize const *const proj, FSize *const left, FSize *const right);

/**
 * @brief Decompose the left and right frustum planes of a perspective projection matrix (left-handed, [0, 1] (ZO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param left Destination for the left frustum plane value.
 * @param right Destination for the right frustum plane value.
 */
void math_cam_persp_decomp_x_lh_zo_1(FSize const *const proj, FSize *const left, FSize *const right);

/**
 * @brief Decompose the left and right frustum planes of a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 *        Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param left Destination for the left frustum plane value.
 * @param right Destination for the right frustum plane value.
 */
void math_cam_persp_decomp_x_rh_no_1(FSize const *const proj, FSize *const left, FSize *const right);

/**
 * @brief Decompose the left and right frustum planes of a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 *        Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param left Destination for the left frustum plane value.
 * @param right Destination for the right frustum plane value.
 */
void math_cam_persp_decomp_x_rh_zo_1(FSize const *const proj, FSize *const left, FSize *const right);

/**
 * @brief Decompose the top and bottom frustum planes of a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 *        Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param top Destination for the top frustum plane value.
 * @param bottom Destination for the bottom frustum plane value.
 */
void math_cam_persp_decomp_y_lh_no_1(FSize const *const proj, FSize *const top, FSize *const bottom);

/**
 * @brief Decompose the top and bottom frustum planes of a perspective projection matrix (left-handed, [0, 1] (ZO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param top Destination for the top frustum plane value.
 * @param bottom Destination for the bottom frustum plane value.
 */
void math_cam_persp_decomp_y_lh_zo_1(FSize const *const proj, FSize *const top, FSize *const bottom);

/**
 * @brief Decompose the top and bottom frustum planes of a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 *        Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param top Destination for the top frustum plane value.
 * @param bottom Destination for the bottom frustum plane value.
 */
void math_cam_persp_decomp_y_rh_no_1(FSize const *const proj, FSize *const top, FSize *const bottom);

/**
 * @brief Decompose the top and bottom frustum planes of a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 *        Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param top Destination for the top frustum plane value.
 * @param bottom Destination for the bottom frustum plane value.
 */
void math_cam_persp_decomp_y_rh_zo_1(FSize const *const proj, FSize *const top, FSize *const bottom);

/**
 * @brief Decompose the near and far frustum planes of a perspective projection matrix (left-handed, [-1, 1] (NO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param near_z Destination for the near z frustum plane value.
 * @param far_z Destination for the far z frustum plane value.
 */
void math_cam_persp_decomp_z_lh_no_1(FSize const *const proj, FSize *const near_z, FSize *const far_z);

/**
 * @brief Decompose the near and far frustum planes of a perspective projection matrix (left-handed, [0, 1] (ZO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param near_z Destination for the near z frustum plane value.
 * @param far_z Destination for the far z frustum plane value.
 */
void math_cam_persp_decomp_z_lh_zo_1(FSize const *const proj, FSize *const near_z, FSize *const far_z);

/**
 * @brief Decompose the near and far frustum planes of a perspective projection matrix (right-handed, [-1, 1] (NO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param near_z Destination for the near z frustum plane value.
 * @param far_z Destination for the far z frustum plane value.
 */
void math_cam_persp_decomp_z_rh_no_1(FSize const *const proj, FSize *const near_z, FSize *const far_z);

/**
 * @brief Decompose the near and far frustum planes of a perspective projection matrix (right-handed, [0, 1] (ZO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param near_z Destination for the near z frustum plane value.
 * @param far_z Destination for the far z frustum plane value.
 */
void math_cam_persp_decomp_z_rh_zo_1(FSize const *const proj, FSize *const near_z, FSize *const far_z);

/**
 * @brief Decompose a perspective projection matrix into a six-element plane array (left-handed, [-1, 1] (NO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 6 contiguous FSize (near, far, top, bottom, left, right).
 */
void math_cam_persp_decompv_lh_no_1(FSize const *const proj, FSize *const dest);

/**
 * @brief Decompose a perspective projection matrix into a six-element plane array (left-handed, [0, 1] (ZO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 6 contiguous FSize (near, far, top, bottom, left, right).
 */
void math_cam_persp_decompv_lh_zo_1(FSize const *const proj, FSize *const dest);

/**
 * @brief Decompose a perspective projection matrix into a six-element plane array (right-handed, [-1, 1] (NO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 6 contiguous FSize (near, far, top, bottom, left, right).
 */
void math_cam_persp_decompv_rh_no_1(FSize const *const proj, FSize *const dest);

/**
 * @brief Decompose a perspective projection matrix into a six-element plane array (right-handed, [0, 1] (ZO) depth). Multi-output decomposition: provided only in the _1 out-pointer form.
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 6 contiguous FSize (near, far, top, bottom, left, right).
 */
void math_cam_persp_decompv_rh_zo_1(FSize const *const proj, FSize *const dest);

/**
 * @brief Extract the vertical field of view from a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_fovy_lh_no_1(FSize const *const proj);

/**
 * @brief Extract the vertical field of view from a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_fovy_lh_no_2(Mat4 const proj);

/**
 * @brief Extract the vertical field of view from a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_fovy_lh_zo_1(FSize const *const proj);

/**
 * @brief Extract the vertical field of view from a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_fovy_lh_zo_2(Mat4 const proj);

/**
 * @brief Extract the vertical field of view from a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_fovy_rh_no_1(FSize const *const proj);

/**
 * @brief Extract the vertical field of view from a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_fovy_rh_no_2(Mat4 const proj);

/**
 * @brief Extract the vertical field of view from a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_persp_fovy_rh_zo_1(FSize const *const proj);

/**
 * @brief Extract the vertical field of view from a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_persp_fovy_rh_zo_2(Mat4 const proj);

/**
 * @brief Move the far clipping plane of a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param delta_far Signed distance to move the far plane.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_persp_move_far_lh_no_1(FSize const *const proj, FSize const delta_far, FSize *const dest);

/**
 * @brief Move the far clipping plane of a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @param delta_far Signed distance to move the far plane.
 * @return Resulting Mat4.
 */
Mat4 math_cam_persp_move_far_lh_no_2(Mat4 const proj, FSize const delta_far);

/**
 * @brief Move the far clipping plane of a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param delta_far Signed distance to move the far plane.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_persp_move_far_lh_zo_1(FSize const *const proj, FSize const delta_far, FSize *const dest);

/**
 * @brief Move the far clipping plane of a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @param delta_far Signed distance to move the far plane.
 * @return Resulting Mat4.
 */
Mat4 math_cam_persp_move_far_lh_zo_2(Mat4 const proj, FSize const delta_far);

/**
 * @brief Move the far clipping plane of a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param delta_far Signed distance to move the far plane.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_persp_move_far_rh_no_1(FSize const *const proj, FSize const delta_far, FSize *const dest);

/**
 * @brief Move the far clipping plane of a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @param delta_far Signed distance to move the far plane.
 * @return Resulting Mat4.
 */
Mat4 math_cam_persp_move_far_rh_no_2(Mat4 const proj, FSize const delta_far);

/**
 * @brief Move the far clipping plane of a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param delta_far Signed distance to move the far plane.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_persp_move_far_rh_zo_1(FSize const *const proj, FSize const delta_far, FSize *const dest);

/**
 * @brief Move the far clipping plane of a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @param delta_far Signed distance to move the far plane.
 * @return Resulting Mat4.
 */
Mat4 math_cam_persp_move_far_rh_zo_2(Mat4 const proj, FSize const delta_far);

/**
 * @brief Compute the near/far plane sizes of a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param fovy Vertical field of view in radians.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_cam_persp_sizes_lh_no_1(FSize const *const proj, FSize const fovy, FSize *const dest);

/**
 * @brief Compute the near/far plane sizes of a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @param fovy Vertical field of view in radians.
 * @return Resulting Vec4.
 */
Vec4 math_cam_persp_sizes_lh_no_2(Mat4 const proj, FSize const fovy);

/**
 * @brief Compute the near/far plane sizes of a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param fovy Vertical field of view in radians.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_cam_persp_sizes_lh_zo_1(FSize const *const proj, FSize const fovy, FSize *const dest);

/**
 * @brief Compute the near/far plane sizes of a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @param fovy Vertical field of view in radians.
 * @return Resulting Vec4.
 */
Vec4 math_cam_persp_sizes_lh_zo_2(Mat4 const proj, FSize const fovy);

/**
 * @brief Compute the near/far plane sizes of a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param fovy Vertical field of view in radians.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_cam_persp_sizes_rh_no_1(FSize const *const proj, FSize const fovy, FSize *const dest);

/**
 * @brief Compute the near/far plane sizes of a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @param fovy Vertical field of view in radians.
 * @return Resulting Vec4.
 */
Vec4 math_cam_persp_sizes_rh_no_2(Mat4 const proj, FSize const fovy);

/**
 * @brief Compute the near/far plane sizes of a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param fovy Vertical field of view in radians.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_cam_persp_sizes_rh_zo_1(FSize const *const proj, FSize const fovy, FSize *const dest);

/**
 * @brief Compute the near/far plane sizes of a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @param fovy Vertical field of view in radians.
 * @return Resulting Vec4.
 */
Vec4 math_cam_persp_sizes_rh_zo_2(Mat4 const proj, FSize const fovy);

/**
 * @brief Build a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_val Near clipping plane distance.
 * @param far_val Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_lh_no_1(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val, FSize *const dest);

/**
 * @brief Build a perspective projection matrix (left-handed, [-1, 1] (NO) depth).
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_val Near clipping plane distance.
 * @param far_val Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_perspective_lh_no_2(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val);

/**
 * @brief Build a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_val Near clipping plane distance.
 * @param far_val Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_lh_zo_1(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val, FSize *const dest);

/**
 * @brief Build a perspective projection matrix (left-handed, [0, 1] (ZO) depth).
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_val Near clipping plane distance.
 * @param far_val Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_perspective_lh_zo_2(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val);

/**
 * @brief Resize a perspective projection matrix for a new aspect ratio (left-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param aspect New aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_resize_lh_no_1(FSize const *const proj, FSize const aspect, FSize *const dest);

/**
 * @brief Resize a perspective projection matrix for a new aspect ratio (left-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @param aspect New aspect ratio (width / height).
 * @return Resulting Mat4.
 */
Mat4 math_cam_perspective_resize_lh_no_2(Mat4 const proj, FSize const aspect);

/**
 * @brief Resize a perspective projection matrix for a new aspect ratio (left-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param aspect New aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_resize_lh_zo_1(FSize const *const proj, FSize const aspect, FSize *const dest);

/**
 * @brief Resize a perspective projection matrix for a new aspect ratio (left-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @param aspect New aspect ratio (width / height).
 * @return Resulting Mat4.
 */
Mat4 math_cam_perspective_resize_lh_zo_2(Mat4 const proj, FSize const aspect);

/**
 * @brief Resize a perspective projection matrix for a new aspect ratio (right-handed, [-1, 1] (NO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param aspect New aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_resize_rh_no_1(FSize const *const proj, FSize const aspect, FSize *const dest);

/**
 * @brief Resize a perspective projection matrix for a new aspect ratio (right-handed, [-1, 1] (NO) depth).
 * @param proj Source projection matrix.
 * @param aspect New aspect ratio (width / height).
 * @return Resulting Mat4.
 */
Mat4 math_cam_perspective_resize_rh_no_2(Mat4 const proj, FSize const aspect);

/**
 * @brief Resize a perspective projection matrix for a new aspect ratio (right-handed, [0, 1] (ZO) depth).
 * @param proj Raw source matrix (16 contiguous FSize, column-major).
 * @param aspect New aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_resize_rh_zo_1(FSize const *const proj, FSize const aspect, FSize *const dest);

/**
 * @brief Resize a perspective projection matrix for a new aspect ratio (right-handed, [0, 1] (ZO) depth).
 * @param proj Source projection matrix.
 * @param aspect New aspect ratio (width / height).
 * @return Resulting Mat4.
 */
Mat4 math_cam_perspective_resize_rh_zo_2(Mat4 const proj, FSize const aspect);

/**
 * @brief Build a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_val Near clipping plane distance.
 * @param far_val Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_rh_no_1(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val, FSize *const dest);

/**
 * @brief Build a perspective projection matrix (right-handed, [-1, 1] (NO) depth).
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_val Near clipping plane distance.
 * @param far_val Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_perspective_rh_no_2(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val);

/**
 * @brief Build a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_val Near clipping plane distance.
 * @param far_val Far clipping plane distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_rh_zo_1(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val, FSize *const dest);

/**
 * @brief Build a perspective projection matrix (right-handed, [0, 1] (ZO) depth).
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_val Near clipping plane distance.
 * @param far_val Far clipping plane distance.
 * @return Resulting Mat4.
 */
Mat4 math_cam_perspective_rh_zo_2(FSize const fovy, FSize const aspect, FSize const near_val, FSize const far_val);

/**
 * @brief Project a world-space point to screen space ([-1, 1] (NO) depth).
 * @param pos Raw source position (3 contiguous FSize).
 * @param m Raw source matrix (16 contiguous FSize, column-major).
 * @param vp Raw viewport (4 contiguous FSize; x, y, width, height).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_cam_project_no_1(FSize const *const pos, FSize const *const m, FSize const *const vp, FSize *const dest);

/**
 * @brief Project a world-space point to screen space ([-1, 1] (NO) depth).
 * @param pos Source position.
 * @param m Model-view-projection matrix.
 * @param vp Viewport (x, y, width, height).
 * @return Resulting Vec3.
 */
Vec3 math_cam_project_no_2(Vec3 const pos, Mat4 const m, Vec4 const vp);

/**
 * @brief Compute the projected Z (depth) of a world-space point ([-1, 1] (NO) depth).
 * @param pos Raw source position (3 contiguous FSize).
 * @param m Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_project_z_no_1(FSize const *const pos, FSize const *const m);

/**
 * @brief Compute the projected Z (depth) of a world-space point ([-1, 1] (NO) depth).
 * @param pos Source position.
 * @param m Model-view-projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_project_z_no_2(Vec3 const pos, Mat4 const m);

/**
 * @brief Compute the projected Z (depth) of a world-space point ([0, 1] (ZO) depth).
 * @param pos Raw source position (3 contiguous FSize).
 * @param m Raw source matrix (16 contiguous FSize, column-major).
 * @return Result as FSize.
 */
FSize math_cam_project_z_zo_1(FSize const *const pos, FSize const *const m);

/**
 * @brief Compute the projected Z (depth) of a world-space point ([0, 1] (ZO) depth).
 * @param pos Source position.
 * @param m Model-view-projection matrix.
 * @return Result as FSize.
 */
FSize math_cam_project_z_zo_2(Vec3 const pos, Mat4 const m);

/**
 * @brief Project a world-space point to screen space ([0, 1] (ZO) depth).
 * @param pos Raw source position (3 contiguous FSize).
 * @param m Raw source matrix (16 contiguous FSize, column-major).
 * @param vp Raw viewport (4 contiguous FSize; x, y, width, height).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_cam_project_zo_1(FSize const *const pos, FSize const *const m, FSize const *const vp, FSize *const dest);

/**
 * @brief Project a world-space point to screen space ([0, 1] (ZO) depth).
 * @param pos Source position.
 * @param m Model-view-projection matrix.
 * @param vp Viewport (x, y, width, height).
 * @return Resulting Vec3.
 */
Vec3 math_cam_project_zo_2(Vec3 const pos, Mat4 const m, Vec4 const vp);

/**
 * @brief Unproject a screen-space point to world space via a precomputed inverse matrix ([-1, 1] (NO) depth).
 * @param pos Raw source position (3 contiguous FSize).
 * @param inv_mat Raw source matrix (16 contiguous FSize, column-major).
 * @param vp Raw viewport (4 contiguous FSize; x, y, width, height).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_cam_unprojecti_no_1(FSize const *const pos, FSize const *const inv_mat, FSize const *const vp, FSize *const dest);

/**
 * @brief Unproject a screen-space point to world space via a precomputed inverse matrix ([-1, 1] (NO) depth).
 * @param pos Source position.
 * @param inv_mat Precomputed inverse model-view-projection matrix.
 * @param vp Viewport (x, y, width, height).
 * @return Resulting Vec3.
 */
Vec3 math_cam_unprojecti_no_2(Vec3 const pos, Mat4 const inv_mat, Vec4 const vp);

/**
 * @brief Unproject a screen-space point to world space via a precomputed inverse matrix ([0, 1] (ZO) depth).
 * @param pos Raw source position (3 contiguous FSize).
 * @param inv_mat Raw source matrix (16 contiguous FSize, column-major).
 * @param vp Raw viewport (4 contiguous FSize; x, y, width, height).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_cam_unprojecti_zo_1(FSize const *const pos, FSize const *const inv_mat, FSize const *const vp, FSize *const dest);

/**
 * @brief Unproject a screen-space point to world space via a precomputed inverse matrix ([0, 1] (ZO) depth).
 * @param pos Source position.
 * @param inv_mat Precomputed inverse model-view-projection matrix.
 * @param vp Viewport (x, y, width, height).
 * @return Resulting Vec3.
 */
Vec3 math_cam_unprojecti_zo_2(Vec3 const pos, Mat4 const inv_mat, Vec4 const vp);

#endif // MATH_CLIPSPACE_H