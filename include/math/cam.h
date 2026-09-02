/*
 * cam.h - Camera projection and view matrices for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled camera API: frustum/ortho/perspective
 *     projections (with the aabb, default, infinite, and resize variants),
 *     look/look_anyup/lookat view matrices, the far-plane movers, and the
 *     perspective decomposition/getters (fovy, aspect, sizes, decomp*)
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Producers build a matrix from parameters: _1 writes a caller destination, _2
 *     returns a Mat4; single-scalar getters return an FSize in both variants
 *
 * Usage Examples:
 *   @code
 *   Vec3 const eye    = { 0.0, 0.0, 5.0 };
 *   Vec3 const center = { 0.0, 0.0, 0.0 };
 *   Vec3 const up     = { 0.0, 1.0, 0.0 };
 *   Mat4 const view   = math_cam_lookat_2(eye, center, up);
 *   Mat4 const proj   = math_cam_perspective_2(1.0472, 1.777, 0.1, 100.0);
 *   @endcode
 *
 * See Also:
 *   - math_quat_look_2 (quat.h) builds a view matrix from an eye position and an orientation
 *     quaternion - the camera builder that lives with the quaternions rather than here.
 *
 * Clip Control:
 *   - Every unsuffixed function here is RIGHT-HANDED with [-1, 1] depth (OpenGL), and
 *     that is PINNED: the wrappers call cglm's explicit _rh_no entry points, never the
 *     unsuffixed ones. cglm's unsuffixed glmc_perspective/lookat/ortho resolve the clip
 *     convention inside the LIBRARY at its build time, so under a system cglm they would
 *     mean whatever the distro chose. For [0, 1] depth or left-handed (Vulkan, Direct3D)
 *     use the clipspace.h variants, which say their convention in the name.
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - Degenerate input is NOT rejected: a singular matrix, a zero aspect or a zeroed
 *     projection propagate Inf/NaN through the result. Validate before converting a
 *     result to an integer (a pick index, a tile coordinate) - that conversion is UB.
 *   - The one exception is perspective_resize: when proj[0][0] == 0 (a zeroed or
 *     non-perspective matrix, tested after the F64 -> float conversion) it returns proj
 *     unchanged up to the float round trip - a finite refusal that isfinite() does not
 *     detect. Check proj[0][0] before trusting a resized matrix.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper converts FSize<->float at the boundary and calls a compiled
 *     glmc_* routine. cglm's cam routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Mat4/Vec3/Vec4 types, the raw<->cglm bridges, cglm,
 *     and the error/tracing macros.
 *
 * See cam.c for implementation details.
 */

#ifndef MATH_CAM_H
#define MATH_CAM_H

#include <cglm/call/clipspace/ortho_rh_no.h>
#include <cglm/call/clipspace/persp_rh_no.h>
#include <cglm/call/clipspace/view_rh_no.h>

#include <math/cglm_compat.h>
#include <math/types.h>

/*==============================================================================
 * MARK: - Cam API
 *
 * Raw (_1) variants read and write contiguous FSize (16 for a mat4, 6 for an aabb
 * box of two vec3, 4 for a vec4, 3 for a vec3) in column-major order; the struct
 * (_2) variants read and return a Mat4 value (or an FSize for scalar getters).
 *
 * persp_decomp, persp_decomp_x, persp_decomp_y, persp_decomp_z and persp_decompv emit
 * several heterogeneous outputs at once (clip planes as separate scalars), so they are
 * provided in the _1 out-pointer form only: a single struct return cannot carry them
 * and multiple out-pointers is the clearest interface, like affine's decompose.
 * persp_sizes is a single Vec4 and so offers both variants, like every other op.
 *============================================================================*/

/**
 * @brief Build a raw perspective frustum projection matrix.
 * @param left Left clip plane.
 * @param right Right clip plane.
 * @param bottom Bottom clip plane.
 * @param top Top clip plane.
 * @param near_z Near clip distance.
 * @param far_z Far clip distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_frustum_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Return a perspective frustum projection matrix.
 * @param left Left clip plane.
 * @param right Right clip plane.
 * @param bottom Bottom clip plane.
 * @param top Top clip plane.
 * @param near_z Near clip distance.
 * @param far_z Far clip distance.
 * @return Projection Mat4.
 */
Mat4 math_cam_frustum_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Build a raw right-handed view matrix from a look direction.
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw look direction (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_1(FSize const *const eye, FSize const *const dir, FSize const *const up, FSize *const dest);

/**
 * @brief Return a right-handed view matrix from a look direction.
 * @param eye Eye position.
 * @param dir Look direction.
 * @param up Up direction.
 * @return View Mat4.
 */
Mat4 math_cam_look_2(Vec3 const eye, Vec3 const dir, Vec3 const up);

/**
 * @brief Build a raw right-handed view matrix from a look direction, deriving up.
 * @param eye Raw eye position (3 contiguous FSize).
 * @param dir Raw look direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_look_anyup_1(FSize const *const eye, FSize const *const dir, FSize *const dest);

/**
 * @brief Return a right-handed view matrix from a look direction, deriving up.
 * @param eye Eye position.
 * @param dir Look direction.
 * @return View Mat4.
 */
Mat4 math_cam_look_anyup_2(Vec3 const eye, Vec3 const dir);

/**
 * @brief Build a raw right-handed look-at view matrix.
 * @param eye Raw eye position (3 contiguous FSize).
 * @param center Raw target position (3 contiguous FSize).
 * @param up Raw up direction (3 contiguous FSize).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_lookat_1(FSize const *const eye, FSize const *const center, FSize const *const up, FSize *const dest);

/**
 * @brief Return a right-handed look-at view matrix.
 * @param eye Eye position.
 * @param center Target position.
 * @param up Up direction.
 * @return View Mat4.
 */
Mat4 math_cam_lookat_2(Vec3 const eye, Vec3 const center, Vec3 const up);

/**
 * @brief Build a raw orthographic projection matrix.
 * @param left Left clip plane.
 * @param right Right clip plane.
 * @param bottom Bottom clip plane.
 * @param top Top clip plane.
 * @param near_z Near clip distance.
 * @param far_z Far clip distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_1(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Return an orthographic projection matrix.
 * @param left Left clip plane.
 * @param right Right clip plane.
 * @param bottom Bottom clip plane.
 * @param top Top clip plane.
 * @param near_z Near clip distance.
 * @param far_z Far clip distance.
 * @return Projection Mat4.
 */
Mat4 math_cam_ortho_2(FSize const left, FSize const right, FSize const bottom, FSize const top, FSize const near_z, FSize const far_z);

/**
 * @brief Build a raw orthographic projection matrix that bounds an AABB.
 * @param box Raw AABB corners (6 contiguous FSize: min xyz then max xyz).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_1(FSize const *const box, FSize *const dest);

/**
 * @brief Return an orthographic projection matrix that bounds an AABB.
 * @param box Bounding box (min and max corners).
 * @return Projection Mat4.
 */
Mat4 math_cam_ortho_aabb_2(Box const box);

/**
 * @brief Build a raw orthographic AABB projection matrix with uniform padding.
 * @param box Raw AABB corners (6 contiguous FSize: min xyz then max xyz).
 * @param padding Uniform padding added around the box.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_p_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Return an orthographic AABB projection matrix with uniform padding.
 * @param box Bounding box (min and max corners).
 * @param padding Uniform padding added around the box.
 * @return Projection Mat4.
 */
Mat4 math_cam_ortho_aabb_p_2(Box const box, FSize const padding);

/**
 * @brief Build a raw orthographic AABB projection matrix, padding the z axis only.
 * @param box Raw AABB corners (6 contiguous FSize: min xyz then max xyz).
 * @param padding Padding added along the z axis.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_aabb_pz_1(FSize const *const box, FSize const padding, FSize *const dest);

/**
 * @brief Return an orthographic AABB projection matrix, padding the z axis only.
 * @param box Bounding box (min and max corners).
 * @param padding Padding added along the z axis.
 * @return Projection Mat4.
 */
Mat4 math_cam_ortho_aabb_pz_2(Box const box, FSize const padding);

/**
 * @brief Build a raw default orthographic projection matrix for an aspect ratio.
 * @param aspect Aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_1(FSize const aspect, FSize *const dest);

/**
 * @brief Return a default orthographic projection matrix for an aspect ratio.
 * @param aspect Aspect ratio (width / height).
 * @return Projection Mat4.
 */
Mat4 math_cam_ortho_default_2(FSize const aspect);

/**
 * @brief Build a raw sized default orthographic projection matrix.
 * @param aspect Aspect ratio (width / height).
 * @param size Half-height of the view volume.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_ortho_default_s_1(FSize const aspect, FSize const size, FSize *const dest);

/**
 * @brief Return a sized default orthographic projection matrix.
 * @param aspect Aspect ratio (width / height).
 * @param size Half-height of the view volume.
 * @return Projection Mat4.
 */
Mat4 math_cam_ortho_default_s_2(FSize const aspect, FSize const size);

/**
 * @brief Aspect ratio of a raw perspective projection matrix.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @return Aspect ratio as FSize.
 */
FSize math_cam_persp_aspect_1(FSize const *const proj);

/**
 * @brief Aspect ratio of a perspective projection matrix.
 * @param proj Projection matrix.
 * @return Aspect ratio as FSize.
 */
FSize math_cam_persp_aspect_2(Mat4 const proj);

/**
 * @brief Decompose a raw perspective projection matrix into its six clip planes.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @param near_z Destination near clip distance.
 * @param far_z Destination far clip distance.
 * @param top Destination top clip plane.
 * @param bottom Destination bottom clip plane.
 * @param left Destination left clip plane.
 * @param right Destination right clip plane.
 */
void math_cam_persp_decomp_1(FSize const *const proj, FSize *const near_z, FSize *const far_z, FSize *const top, FSize *const bottom, FSize *const left, FSize *const right);

/**
 * @brief Far clip distance of a raw perspective projection matrix.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @return Far clip distance as FSize.
 */
FSize math_cam_persp_decomp_far_1(FSize const *const proj);

/**
 * @brief Far clip distance of a perspective projection matrix.
 * @param proj Projection matrix.
 * @return Far clip distance as FSize.
 */
FSize math_cam_persp_decomp_far_2(Mat4 const proj);

/**
 * @brief Near clip distance of a raw perspective projection matrix.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @return Near clip distance as FSize.
 */
FSize math_cam_persp_decomp_near_1(FSize const *const proj);

/**
 * @brief Near clip distance of a perspective projection matrix.
 * @param proj Projection matrix.
 * @return Near clip distance as FSize.
 */
FSize math_cam_persp_decomp_near_2(Mat4 const proj);

/**
 * @brief Decompose the left and right clip planes of a raw perspective matrix.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @param left Destination left clip plane.
 * @param right Destination right clip plane.
 */
void math_cam_persp_decomp_x_1(FSize const *const proj, FSize *const left, FSize *const right);

/**
 * @brief Decompose the top and bottom clip planes of a raw perspective matrix.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @param top Destination top clip plane.
 * @param bottom Destination bottom clip plane.
 */
void math_cam_persp_decomp_y_1(FSize const *const proj, FSize *const top, FSize *const bottom);

/**
 * @brief Decompose the near and far clip distances of a raw perspective matrix.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @param near_z Destination near clip distance.
 * @param far_z Destination far clip distance.
 */
void math_cam_persp_decomp_z_1(FSize const *const proj, FSize *const near_z, FSize *const far_z);

/**
 * @brief Decompose a raw perspective matrix into a six-element clip-plane array.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @param dest Destination of 6 contiguous FSize (near, far, top, bottom, left, right).
 */
void math_cam_persp_decompv_1(FSize const *const proj, FSize *const dest);

/**
 * @brief Vertical field of view of a raw perspective projection matrix.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @return Vertical field of view in radians as FSize.
 */
FSize math_cam_persp_fovy_1(FSize const *const proj);

/**
 * @brief Vertical field of view of a perspective projection matrix.
 * @param proj Projection matrix.
 * @return Vertical field of view in radians as FSize.
 */
FSize math_cam_persp_fovy_2(Mat4 const proj);

/**
 * @brief Move the far clip plane of a raw perspective projection matrix.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @param delta_far Signed distance to move the far plane by.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_persp_move_far_1(FSize const *const proj, FSize const delta_far, FSize *const dest);

/**
 * @brief Return a perspective projection matrix with its far clip plane moved.
 * @param proj Projection matrix.
 * @param delta_far Signed distance to move the far plane by.
 * @return Adjusted projection Mat4.
 */
Mat4 math_cam_persp_move_far_2(Mat4 const proj, FSize const delta_far);

/**
 * @brief Focal sizes of a raw perspective matrix at a given field of view.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major).
 * @param fovy Vertical field of view in radians.
 * @param dest Destination of 4 contiguous FSize: near width, near height, far width, far height.
 */
void math_cam_persp_sizes_1(FSize const *const proj, FSize const fovy, FSize *const dest);

/**
 * @brief Focal sizes of a perspective matrix at a given field of view.
 * @param proj Projection matrix.
 * @param fovy Vertical field of view in radians.
 * @return The focal sizes as (near width, near height, far width, far height) in x, y, z, w.
 */
Vec4 math_cam_persp_sizes_2(Mat4 const proj, FSize const fovy);

/**
 * @brief Build a raw right-handed perspective projection matrix.
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_z Near clip distance.
 * @param far_z Far clip distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_1(FSize const fovy, FSize const aspect, FSize const near_z, FSize const far_z, FSize *const dest);

/**
 * @brief Return a right-handed perspective projection matrix.
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_z Near clip distance.
 * @param far_z Far clip distance.
 * @return Projection Mat4.
 */
Mat4 math_cam_perspective_2(FSize const fovy, FSize const aspect, FSize const near_z, FSize const far_z);

/**
 * @brief Build a raw default perspective projection matrix for an aspect ratio.
 * @param aspect Aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_default_1(FSize const aspect, FSize *const dest);

/**
 * @brief Return a default perspective projection matrix for an aspect ratio.
 * @param aspect Aspect ratio (width / height).
 * @return Projection Mat4.
 */
Mat4 math_cam_perspective_default_2(FSize const aspect);

/**
 * @brief Build a raw default infinite-far perspective projection matrix.
 * @param aspect Aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_default_infinite_1(FSize const aspect, FSize *const dest);

/**
 * @brief Return a default infinite-far perspective projection matrix.
 * @param aspect Aspect ratio (width / height).
 * @return Projection Mat4.
 */
Mat4 math_cam_perspective_default_infinite_2(FSize const aspect);

/**
 * @brief Build a raw perspective projection matrix with an infinite far plane.
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_z Near clip distance.
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_infinite_1(FSize const fovy, FSize const aspect, FSize const near_z, FSize *const dest);

/**
 * @brief Return a perspective projection matrix with an infinite far plane.
 * @param fovy Vertical field of view in radians.
 * @param aspect Aspect ratio (width / height).
 * @param near_z Near clip distance.
 * @return Projection Mat4.
 */
Mat4 math_cam_perspective_infinite_2(FSize const fovy, FSize const aspect, FSize const near_z);

/**
 * @brief Resize a raw perspective projection matrix for a new aspect ratio.
 * @param proj Raw projection matrix (16 contiguous FSize, column-major); copied to dest
 *        unchanged when its [0][0] entry is 0.
 * @param aspect New aspect ratio (width / height).
 * @param dest Destination of 16 contiguous FSize (column-major).
 */
void math_cam_perspective_resize_1(FSize const *const proj, FSize const aspect, FSize *const dest);

/**
 * @brief Return a perspective projection matrix resized for a new aspect ratio.
 * @param proj Projection matrix; returned unchanged when its [0][0] entry is 0.
 * @param aspect New aspect ratio (width / height).
 * @return Resized projection Mat4.
 */
Mat4 math_cam_perspective_resize_2(Mat4 const proj, FSize const aspect);

#endif // MATH_CAM_H