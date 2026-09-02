/*
 * project.h - Screen-space projection helpers for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_*project* API: object-space to
 *     window-space projection (project, project_z) and the window-space to
 *     object-space inverse (unproject, unprojecti)
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Vec3 (or an FSize for the scalar depth result)
 *
 * Usage Examples:
 *   @code
 *   Vec3 const world  = { 1.0, 2.0, -5.0 };
 *   Vec4 const vp     = { 0.0, 0.0, 800.0, 600.0 };
 *   Vec3 const window = math_project_2(world, mvp, vp);
 *   Vec3 const back   = math_project_unproject_2(window, mvp, vp);
 *   @endcode
 *
 * Clip Control:
 *   - project / project_z / unprojecti are PINNED to [-1, 1] depth (the _no cglm
 *     entry points), for the reason cam.h gives: the unsuffixed cglm calls resolve
 *     the convention inside the library at its build time. unproject has no
 *     clip-suffixed entry point in cglm 0.9.6 and inherits the library's default,
 *     which is [-1, 1] in every distro build known. The clipspace.h variants exist
 *     for [0, 1] depth.
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - Degenerate input is NOT rejected: a singular matrix, a zero aspect or a zeroed
 *     projection propagate Inf/NaN through the result. Validate before converting a
 *     result to an integer (a pick index, a tile coordinate) - that conversion is UB.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper converts FSize<->float at the boundary and calls a compiled
 *     glmc_* routine. cglm's project routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Vec3/Vec4/Mat4 types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * See project.c for implementation details.
 */

#ifndef MATH_PROJECT_H
#define MATH_PROJECT_H

#include <cglm/call/clipspace/project_no.h>

#include <math/types.h>

/*==============================================================================
 * MARK: - Project API
 *
 * The transform pos is a 3D point, m a 4x4 model-view-projection matrix, and vp
 * a viewport as (x, y, width, height). Raw (_1) variants read 3 contiguous FSize
 * for pos, 16 for m, and 4 for vp, then write 3 contiguous FSize to dest; the
 * struct (_2) variants read Vec3/Mat4/Vec4 values and return a Vec3 (or an FSize
 * for the scalar depth result).
 *============================================================================*/

/**
 * @brief Project a raw object-space point to window space through a raw matrix.
 * @param pos Raw object-space point (3 contiguous FSize).
 * @param m Raw model-view-projection matrix (16 contiguous FSize, column-major).
 * @param vp Raw viewport as x, y, width, height (4 contiguous FSize).
 * @param dest Destination window-space point (3 contiguous FSize).
 */
void math_project_1(FSize const *const pos, FSize const *const m, FSize const *const vp, FSize *const dest);

/**
 * @brief Return the window-space projection of an object-space point.
 * @param pos Object-space point.
 * @param m Model-view-projection matrix.
 * @param vp Viewport as x, y, width, height.
 * @return Window-space Vec3 (x, y in pixels, z in depth range).
 */
Vec3 math_project_2(Vec3 const pos, Mat4 const m, Vec4 const vp);

/**
 * @brief Build a raw picking-region matrix from a raw center, size, and viewport.
 * @param center Raw pick-region center in window coordinates (2 contiguous FSize).
 * @param size Raw pick-region size in window units (2 contiguous FSize).
 * @param vp Raw viewport as x, y, width, height (4 contiguous FSize).
 * @param dest Destination picking matrix (16 contiguous FSize, column-major).
 */
void math_project_pickmatrix_1(FSize const *const center, FSize const *const size, FSize const *const vp, FSize *const dest);

/**
 * @brief Return the picking-region matrix for a center, size, and viewport.
 * @param center Pick-region center in window coordinates.
 * @param size Pick-region size in window units.
 * @param vp Viewport as x, y, width, height.
 * @return Picking Mat4.
 */
Mat4 math_project_pickmatrix_2(Vec2 const center, Vec2 const size, Vec4 const vp);

/**
 * @brief Unproject a raw window-space point to object space through a raw matrix.
 * @param pos Raw window-space point (3 contiguous FSize).
 * @param m Raw model-view-projection matrix (16 contiguous FSize, column-major);
 *          inverted internally.
 * @param vp Raw viewport as x, y, width, height (4 contiguous FSize).
 * @param dest Destination object-space point (3 contiguous FSize).
 */
void math_project_unproject_1(FSize const *const pos, FSize const *const m, FSize const *const vp, FSize *const dest);

/**
 * @brief Return the object-space unprojection of a window-space point.
 * @param pos Window-space point.
 * @param m Model-view-projection matrix; inverted internally.
 * @param vp Viewport as x, y, width, height.
 * @return Object-space Vec3.
 */
Vec3 math_project_unproject_2(Vec3 const pos, Mat4 const m, Vec4 const vp);

/**
 * @brief Unproject a raw window-space point using a pre-inverted raw matrix.
 * @param pos Raw window-space point (3 contiguous FSize).
 * @param inv_mat Raw inverse model-view-projection matrix (16 contiguous FSize,
 *               column-major).
 * @param vp Raw viewport as x, y, width, height (4 contiguous FSize).
 * @param dest Destination object-space point (3 contiguous FSize).
 */
void math_project_unprojecti_1(FSize const *const pos, FSize const *const inv_mat, FSize const *const vp, FSize *const dest);

/**
 * @brief Return the object-space unprojection using a pre-inverted matrix.
 * @param pos Window-space point.
 * @param inv_mat Inverse model-view-projection matrix.
 * @param vp Viewport as x, y, width, height.
 * @return Object-space Vec3.
 */
Vec3 math_project_unprojecti_2(Vec3 const pos, Mat4 const inv_mat, Vec4 const vp);

/**
 * @brief Project only the window-space depth of a raw object-space point.
 * @param pos Raw object-space point (3 contiguous FSize).
 * @param m Raw model-view-projection matrix (16 contiguous FSize, column-major).
 * @return Window-space depth as FSize.
 */
FSize math_project_z_1(FSize const *const pos, FSize const *const m);

/**
 * @brief Return the window-space depth of an object-space point.
 * @param pos Object-space point.
 * @param m Model-view-projection matrix.
 * @return Window-space depth as FSize.
 */
FSize math_project_z_2(Vec3 const pos, Mat4 const m);

#endif // MATH_PROJECT_H