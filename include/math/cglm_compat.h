/*
 * cglm_compat.h - CFW-side implementations of cglm APIs newer than the 0.9.6 release
 *
 * cglm is a SYSTEM dependency of CFW, pinned to the 0.9.6 that Debian, Ubuntu and MSYS2 ship.
 * The tree this module was written against was a post-0.9.6 git snapshot whose version.h still
 * says 0.9.6, and it grew eight compiled (`glmc_`) entry points that the released library does
 * not export. Linking against the real 0.9.6 fails on exactly those eight. This header carries
 * their bodies, transcribed from the snapshot's own inline definitions, so the facade needs no
 * cglm newer than the one a package manager installs.
 *
 * Features:
 *   - The eight post-0.9.6 bodies: mat3/mat4 textrans, perspective_infinite and its default,
 *     perspective_default (0.9.6 has it only inline), and one perspective_resize serving all
 *     four clip-control call sites (their upstream bodies are byte-identical)
 *   - Called UNCONDITIONALLY: no CGLM_VERSION gate, because both trees claim 0.9.6 and a gated
 *     call is a second code path nothing tests; private `_math_` names cannot collide with any
 *     future cglm
 *   - RH_NO pinned in the names: the snapshot's unsuffixed entry points resolve
 *     CGLM_CONFIG_CLIP_CONTROL inside the LIBRARY at its build time, so a distro built with
 *     CGLM_FORCE_DEPTH_ZERO_TO_ONE would silently change results
 *
 * Usage Examples:
 *   @code
 *   // Inside a math translation unit, in place of the missing glmc_ entry point:
 *   mat4 cd = DEFAULT_INITIALIZATION;
 *
 *   _math_cglm_perspective_infinite_rh_no((float) fovy, (float) aspect, (float) near_z, cd);
 *   @endcode
 *
 * Error Handling:
 *   - None. These are the raw cglm bodies; the public math_* wrappers that call them own the
 *     pointer checks and the degenerate-input contract (see cam.h, clipspace.h, mat3.h, mat4.h).
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function writes a caller-supplied cglm array.
 *
 * Performance Characteristics:
 *   - Static inline, a handful of stores each; identical to the inline forms cglm ships.
 *
 * Dependencies:
 *   - <cglm/cglm.h> for mat3/mat4, glm_mat3_identity, glm_mat4_identity, glm_mat4_zero,
 *     glm_perspective_rh_no and GLM_PI_4f; <math.h> for cosf/sinf/tanf, which cglm's common.h
 *     already provides.
 *   - Included by the four math headers whose sources call it (cam.h, clipspace.h, mat3.h,
 *     mat4.h) - never by types.h, which sits in every module's closure through memory.h.
 *   - The existing test literals in tests/math were produced by the snapshot and are the
 *     differential oracle: a transcription error here fails them.
 */

#ifndef MATH_CGLM_COMPAT_H
#define MATH_CGLM_COMPAT_H

#include <cglm/cglm.h>

/*==============================================================================
 * MARK: - Functions
 *============================================================================*/

// Translated from cglm mat3.h (snapshot): identity, then a 2D scale-rotate-translate.
static inline void _math_cglm_mat3_textrans(float const sx, float const sy, float const rot, float const tx, float const ty, mat3 dest) {
    float const c = cosf(rot);
    float const s = sinf(rot);

    glm_mat3_identity(dest);

    dest[0][0] =  c * sx;
    dest[0][1] = -s * sy;
    dest[1][0] =  s * sx;
    dest[1][1] =  c * sy;
    dest[2][0] =  tx;
    dest[2][1] =  ty;
}

// Translated from cglm mat4.h (snapshot): the mat3 form with the translation in column 3.
static inline void _math_cglm_mat4_textrans(float const sx, float const sy, float const rot, float const tx, float const ty, mat4 dest) {
    float const c = cosf(rot);
    float const s = sinf(rot);

    glm_mat4_identity(dest);

    dest[0][0] =  c * sx;
    dest[0][1] = -s * sy;
    dest[1][0] =  s * sx;
    dest[1][1] =  c * sy;
    dest[3][0] =  tx;
    dest[3][1] =  ty;
}

// Translated from cglm clipspace/persp_rh_no.h (snapshot): far plane at infinity, [-1, 1] depth.
static inline void _math_cglm_perspective_infinite_rh_no(float const fovy, float const aspect, float const near_z, mat4 dest) {
    glm_mat4_zero(dest);

    float const f = 1.0f / tanf(fovy * 0.5f);

    dest[0][0] =  f / aspect;
    dest[1][1] =  f;
    dest[2][2] = -1.0f;
    dest[2][3] = -1.0f;
    dest[3][2] = -2.0f * near_z;
}

// Translated from cglm clipspace/persp_rh_no.h (snapshot): 45 degrees, near plane 0.01.
static inline void _math_cglm_perspective_default_infinite_rh_no(float const aspect, mat4 dest) {
    _math_cglm_perspective_infinite_rh_no(GLM_PI_4f, aspect, 0.01f, dest);
}

/* Translated from cglm clipspace/persp_rh_no.h (0.9.6 has this INLINE but exports no compiled
 * glmc_perspective_default_rh_no - only the clip-control-dispatching glmc_perspective_default).
 * Pinning RH_NO therefore needs a CFW-side body: 45 degrees, near 0.01, far 100. */
static inline void _math_cglm_perspective_default_rh_no(float const aspect, mat4 dest) {
    glm_perspective_rh_no(GLM_PI_4f, aspect, 0.01f, 100.0f, dest);
}

/* Translated from cglm clipspace/persp_*.h (snapshot): rescales the x focal term for a new aspect.
 * All four clip-control variants have this exact body, so one function serves them all. A zero
 * proj[0][0] (a matrix that was never a perspective) is left untouched, as upstream does. */
static inline void _math_cglm_perspective_resize(float const aspect, mat4 proj) {
    if (proj[0][0] == 0.0f) {
        return;
    }

    proj[0][0] = proj[1][1] / aspect;
}

#endif // MATH_CGLM_COMPAT_H