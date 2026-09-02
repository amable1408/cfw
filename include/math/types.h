/*
 * types.h - Shared types and raw<->cglm bridges for the CFW math module
 *
 * Features:
 *   - Framework FSize/ISize superset structs over every cglm vector/matrix type
 *   - Internal static-inline bridge helpers converting each type to/from its raw
 *     cglm array representation (used by every math sub-module)
 *   - Shared math constants
 *
 * Usage Examples:
 *   @code
 *   Vec3 const v = { 1.0, 2.0, 3.0 };
 *   vec3 raw = DEFAULT_INITIALIZATION;
 *   _math_vec3_to_cglm(v, raw);
 *   @endcode
 *
 * Error Handling:
 *   - Bridges perform no validation; callers validate pointers at the public API.
 *
 * Thread Safety:
 *   - All helpers are pure and thread-safe.
 *
 * Memory Management:
 *   - No allocation; every helper operates on values only.
 *
 * Performance Characteristics:
 *   - Bridges are inline element copies with explicit float<->FSize casts.
 *
 * Precision:
 *   - Every cglm-backed operation converts FSize (F64) -> float -> FSize. That is a
 *     PRECISION loss on the way through and a VALUE FLIP for some operations, not just a
 *     rounding: floor / fract / step / smoothstep evaluate on the float, so an F64 that is
 *     0.99999999 rounds to 1.0f and floors to 1 where F64 gives 0; eq / eqv and the *_eps
 *     forms compare at float resolution, so two F64 values closer than one float ulp
 *     compare EQUAL; integer-valued coordinates stay exact only below 2^24 (pixels and
 *     tiles are safe, epoch milliseconds and accumulated F64 sums are not); mods is wrong
 *     past 2^24. The ivec family converts ISize -> int and documents its own domain.
 *   - The conversion itself is defined only for |value| <= FLT_MAX (about 3.4e38): a larger F64
 *     is undefined behaviour at the cast, not a saturation to Inf. Callers own that domain; no
 *     bridge guards it.
 *   - rect.h and scalar.h are native F64 and carry none of this.
 *
 * Dependencies:
 *   - <cglm/call.h> for the raw cglm types and compiled glmc_* API.
 *   - <error/error.h> for error_check_null and the tracing macros it chains in.
 *   - "../types.h" for FSize/ISize/USize and DEFAULT_INITIALIZATION.
 *
 * This is the foundation header every math sub-module includes - and, because memory.h
 * includes the math umbrella, every CFW module as well. That reach is the reason it stays
 * lean: it is not a compat header (that is cglm_compat.h, included only by the four wrappers
 * that need it), and it holds nothing used by fewer than two sub-modules - the bridges and
 * the ivec / swizzle guards each serve several.
 */

#ifndef MATH_TYPES_H
#define MATH_TYPES_H

#include <math.h>
#include <stdlib.h>

#include <cglm/call.h>

#include <error/error.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

#define MATH_DEG_TO_RAD (MATH_PI / 180)
#define MATH_PI 3.141592653589793
#define MATH_RAD_TO_DEG (180 / MATH_PI)
#define MATH_TAU 6.283185307179586

/*==============================================================================
 * MARK: - Types
 *
 * Each struct is a framework superset over the raw cglm array underset. Float
 * types use FSize; integer vectors use ISize. Matrices follow cglm's column-major
 * layout: a MatCxR stores C columns of R rows as m[C][R].
 *============================================================================*/

/**
 * @brief 2D vector; FSize superset over cglm `vec2`.
 */
typedef struct Vec2 { FSize x, y; } Vec2;

/**
 * @brief 3D vector; FSize superset over cglm `vec3`.
 */
typedef struct Vec3 { FSize x, y, z; } Vec3;

/**
 * @brief 4D vector; FSize superset over cglm `vec4`.
 */
typedef struct Vec4 { FSize x, y, z, w; } Vec4;

/**
 * @brief 2D integer vector; ISize superset over cglm `ivec2`.
 */
typedef struct IVec2 { ISize x, y; } IVec2;

/**
 * @brief 3D integer vector; ISize superset over cglm `ivec3`.
 */
typedef struct IVec3 { ISize x, y, z; } IVec3;

/**
 * @brief 4D integer vector; ISize superset over cglm `ivec4`.
 */
typedef struct IVec4 { ISize x, y, z, w; } IVec4;

/**
 * @brief 2x2 column-major matrix (cglm layout); superset over cglm `mat2`.
 */
typedef struct Mat2 { FSize m[2][2]; } Mat2;

/**
 * @brief 3x3 column-major matrix (cglm layout); superset over cglm `mat3`.
 */
typedef struct Mat3 { FSize m[3][3]; } Mat3;

/**
 * @brief 4x4 column-major matrix (cglm layout); superset over cglm `mat4`.
 */
typedef struct Mat4 { FSize m[4][4]; } Mat4;

/**
 * @brief 2-column 3-row column-major matrix; superset over cglm `mat2x3`.
 */
typedef struct Mat2x3 { FSize m[2][3]; } Mat2x3;

/**
 * @brief 2-column 4-row column-major matrix; superset over cglm `mat2x4`.
 */
typedef struct Mat2x4 { FSize m[2][4]; } Mat2x4;

/**
 * @brief 3-column 2-row column-major matrix; superset over cglm `mat3x2`.
 */
typedef struct Mat3x2 { FSize m[3][2]; } Mat3x2;

/**
 * @brief 3-column 4-row column-major matrix; superset over cglm `mat3x4`.
 */
typedef struct Mat3x4 { FSize m[3][4]; } Mat3x4;

/**
 * @brief 4-column 2-row column-major matrix; superset over cglm `mat4x2`.
 */
typedef struct Mat4x2 { FSize m[4][2]; } Mat4x2;

/**
 * @brief 4-column 3-row column-major matrix; superset over cglm `mat4x3`.
 */
typedef struct Mat4x3 { FSize m[4][3]; } Mat4x3;

/**
 * @brief Quaternion (x, y, z, w; w is the real part); superset over cglm `versor`.
 */
typedef struct Quat { FSize x, y, z, w; } Quat;

/**
 * @brief 2D axis-aligned bounding box (min, max corners); over cglm `vec2 aabb[2]`.
 */
typedef struct Aabb2d { Vec2 min, max; } Aabb2d;

/**
 * @brief 2D rectangle as origin (x, y) and size (w, h).
 *
 * Has no cglm counterpart: it is the position+size (x, y, w, h) cousin of Aabb2d's
 * min/max form. Its API in rect.h is implemented directly in FSize arithmetic rather
 * than over a raw<->cglm bridge.
 *
 * NOT layout-compatible with SDL_FRect, despite the matching field names: FSize is F64,
 * so a Rect is four doubles (32 bytes) where SDL_FRect is four floats (16). A cast in
 * either direction is type confusion - reinterpreting half a double as a float one way,
 * reading 32 bytes out of a 16-byte struct the other. Convert field by field.
 */
typedef struct Rect { FSize x, y, w, h; } Rect;

/**
 * @brief 3D axis-aligned bounding box (min, max corners); over cglm `vec3 box[2]`.
 */
typedef struct Box { Vec3 min, max; } Box;

/**
 * @brief Plane as normal (x, y, z) and signed distance (w); over cglm `vec4`.
 */
typedef struct Plane { FSize x, y, z, w; } Plane;

/**
 * @brief The eight corners of a view frustum, as homogeneous points.
 *
 * Order follows cglm: left/right, bottom/top, near/far - corners[0] is left-bottom-near.
 * The raw (_1) forms take the same data as 32 contiguous FSize.
 */
typedef struct FrustumCorners { Vec4 corners[8]; } FrustumCorners;

/**
 * @brief The six planes of a view frustum.
 *
 * Order follows cglm: left, right, bottom, top, near, far. The raw (_1) forms take the
 * same data as 24 contiguous FSize.
 */
typedef struct FrustumPlanes { Plane planes[6]; } FrustumPlanes;

/**
 * @brief The four corners of a frustum split plane (cascaded shadow maps).
 *
 * Order follows cglm: left-bottom, left-top, right-top, right-bottom. The raw (_1) form
 * writes the same data as 16 contiguous FSize.
 */
typedef struct FrustumSplitCorners { Vec4 corners[4]; } FrustumSplitCorners;

/**
 * @brief A circle as center (x, y) and radius r.
 *
 * The raw (_1) forms take the same data as 3 contiguous FSize (x, y, r); bridged to cglm `vec3`
 * by _math_circle_to_cglm.
 */
typedef struct Circle { FSize x, y, r; } Circle;

/**
 * @brief Ray with an origin and a direction; over cglm `vec3` origin + `vec3` dir.
 */
typedef struct Ray { Vec3 origin, direction; } Ray;

/**
 * @brief Sphere as center (x, y, z) and radius (r); bridged to cglm `vec4` by _math_sphere_to_cglm.
 */
typedef struct Sphere { FSize x, y, z, r; } Sphere;

/**
 * @brief Result of a ray/sphere intersection: whether it hit, and both distances when it did.
 *
 * t1 <= t2; t1 < 0 < t2 means the origin is inside the sphere. One cglm edge is passed through:
 * an origin ON the sphere with a direction tangent to it reports hit with t1 = 0 and t2 = NaN
 * (its 0 / 0) - a NaN root is that case, not a wrapper bug. When !hit both are 0 - the
 * wrapper's value: cglm leaves them untouched when there is no real root and writes two negative
 * roots when the sphere is behind the origin, and the wrapper overrides both cases.
 */
typedef struct RaySphereHit { bool hit; FSize t1, t2; } RaySphereHit;

/**
 * @brief Result of a ray/triangle intersection: whether it hit, and the distance when it did.
 *
 * d is 0 on every miss - the wrapper's value, overriding what cglm leaves (untouched, or a
 * distance at or below its 1e-6 epsilon on a grazing miss).
 */
typedef struct RayTriangleHit { bool hit; FSize d; } RayTriangleHit;

/**
 * @brief Result of a 2D refraction: whether it refracted, and the refracted vector when it did.
 *
 * v is the zero vector on total internal reflection (!refracted).
 */
typedef struct Vec2Refraction { bool refracted; Vec2 v; } Vec2Refraction;

/**
 * @brief Result of a 3D refraction: whether it refracted, and the refracted vector when it did.
 *
 * v is the zero vector on total internal reflection (!refracted).
 */
typedef struct Vec3Refraction { bool refracted; Vec3 v; } Vec3Refraction;

/**
 * @brief Result of a 4D refraction: whether it refracted, and the refracted vector when it did.
 *
 * v is the zero vector on total internal reflection (!refracted).
 */
typedef struct Vec4Refraction { bool refracted; Vec4 v; } Vec4Refraction;

/*==============================================================================
 * MARK: - Vector bridges
 *
 * Internal raw <-> cglm converters. Trivial, so they skip tracing per
 * style_guidelines.md section 7. static inline keeps them file-local per
 * translation unit with no link-time duplication.
 *============================================================================*/

static inline void _math_vec2_to_cglm(Vec2 const self, vec2 dest) {
    dest[0] = (float) self.x;
    dest[1] = (float) self.y;
}

static inline Vec2 _math_vec2_from_cglm(vec2 const source) {
    Vec2 const self = { (FSize) source[0], (FSize) source[1] };

    return self;
}

static inline void _math_vec2_raw_to_cglm(FSize const *const source, vec2 dest) {
    dest[0] = (float) source[0];
    dest[1] = (float) source[1];
}

static inline void _math_vec2_raw_from_cglm(vec2 const source, FSize *const dest) {
    dest[0] = (FSize) source[0];
    dest[1] = (FSize) source[1];
}

static inline void _math_vec3_to_cglm(Vec3 const self, vec3 dest) {
    dest[0] = (float) self.x;
    dest[1] = (float) self.y;
    dest[2] = (float) self.z;
}

static inline Vec3 _math_vec3_from_cglm(vec3 const source) {
    Vec3 const self = { (FSize) source[0], (FSize) source[1], (FSize) source[2] };

    return self;
}

static inline void _math_vec3_raw_to_cglm(FSize const *const source, vec3 dest) {
    dest[0] = (float) source[0];
    dest[1] = (float) source[1];
    dest[2] = (float) source[2];
}

static inline void _math_vec3_raw_from_cglm(vec3 const source, FSize *const dest) {
    dest[0] = (FSize) source[0];
    dest[1] = (FSize) source[1];
    dest[2] = (FSize) source[2];
}

static inline void _math_vec4_to_cglm(Vec4 const self, vec4 dest) {
    dest[0] = (float) self.x;
    dest[1] = (float) self.y;
    dest[2] = (float) self.z;
    dest[3] = (float) self.w;
}

static inline Vec4 _math_vec4_from_cglm(vec4 const source) {
    Vec4 const self = { (FSize) source[0], (FSize) source[1], (FSize) source[2], (FSize) source[3] };

    return self;
}

static inline void _math_vec4_raw_to_cglm(FSize const *const source, vec4 dest) {
    dest[0] = (float) source[0];
    dest[1] = (float) source[1];
    dest[2] = (float) source[2];
    dest[3] = (float) source[3];
}

static inline void _math_vec4_raw_from_cglm(vec4 const source, FSize *const dest) {
    dest[0] = (FSize) source[0];
    dest[1] = (FSize) source[1];
    dest[2] = (FSize) source[2];
    dest[3] = (FSize) source[3];
}

/*==============================================================================
 * MARK: - Integer vector bridges
 *============================================================================*/

/* Clamp BOUNDS cross the ISize -> int boundary by saturation, not truncation: a truncated
 * bound silently inverts the clamp (ISIZE_MAX -> -1, 0x100000000 -> 0). Components keep
 * the plain boundary cast; the domain is documented in each ivec header. */
/* The F64 -> float narrowing is defined only inside float range (an F64 past FLT_MAX is undefined
 * at the cast, not a saturation), so a guard that must refuse such a value cannot test the cast's
 * result. This bounds first and yields 0.0f outside the range - NaN fails both comparisons - so a
 * caller then tests the float it will hand to cglm. The bridges do NOT use it: their domain is the
 * caller's (see Precision); it exists for the wrappers that refuse by value. */
static inline float _math_fsize_to_float_bounded(FSize const value) {
    return (value >= -(F64) FLT_MAX && value <= (F64) FLT_MAX) ? (float) value : 0.0f;
}

static inline int _math_ivec_saturate_int(ISize const value) {
    if (value > INT_MAX) { return INT_MAX; }
    if (value < INT_MIN) { return INT_MIN; }

    return (int) value;
}

/* A swizzle mask packs one 2-bit source index per output lane (cglm's GLM_SHUFFLEn). Every
 * MATH_SWIZZLE_* constant is in range for its OWN arity, but the names share one prefix across
 * vec2/vec3/vec4, so a vec4 mask can reach a vec2 wrapper and index v[3] of a 2-float array -
 * a stack over-read. The wrappers refuse any mask with a field at or past `arity`. */
static inline bool _math_swizzle_mask_fits(ISize const mask, int const arity) {
    for (int lane = 0; lane < arity; lane += 1) {
        if (((mask >> (2 * lane)) & 3) >= arity) {
            return false;
        }
    }

    return true;
}

/* The complete set of int divisions that TRAP on x86 (SIGFPE, an uncatchable process kill): a
 * zero divisor, and INT_MIN / -1 whose quotient does not fit. Both `/` and `%` share it. Every
 * ivec div, divs and mod wrapper refuses to the zeroed vector when this is true. */
static inline bool _math_ivec_div_traps(int const dividend, int const divisor) {
    return divisor == 0 || (divisor == -1 && dividend == INT_MIN);
}

static inline void _math_ivec2_to_cglm(IVec2 const self, ivec2 dest) {
    dest[0] = (int) self.x;
    dest[1] = (int) self.y;
}

static inline IVec2 _math_ivec2_from_cglm(ivec2 const source) {
    IVec2 const self = { (ISize) source[0], (ISize) source[1] };

    return self;
}

static inline void _math_ivec2_raw_to_cglm(ISize const *const source, ivec2 dest) {
    dest[0] = (int) source[0];
    dest[1] = (int) source[1];
}

static inline void _math_ivec2_raw_from_cglm(ivec2 const source, ISize *const dest) {
    dest[0] = (ISize) source[0];
    dest[1] = (ISize) source[1];
}

static inline void _math_ivec3_to_cglm(IVec3 const self, ivec3 dest) {
    dest[0] = (int) self.x;
    dest[1] = (int) self.y;
    dest[2] = (int) self.z;
}

static inline IVec3 _math_ivec3_from_cglm(ivec3 const source) {
    IVec3 const self = { (ISize) source[0], (ISize) source[1], (ISize) source[2] };

    return self;
}

static inline void _math_ivec3_raw_to_cglm(ISize const *const source, ivec3 dest) {
    dest[0] = (int) source[0];
    dest[1] = (int) source[1];
    dest[2] = (int) source[2];
}

static inline void _math_ivec3_raw_from_cglm(ivec3 const source, ISize *const dest) {
    dest[0] = (ISize) source[0];
    dest[1] = (ISize) source[1];
    dest[2] = (ISize) source[2];
}

static inline void _math_ivec4_to_cglm(IVec4 const self, ivec4 dest) {
    dest[0] = (int) self.x;
    dest[1] = (int) self.y;
    dest[2] = (int) self.z;
    dest[3] = (int) self.w;
}

static inline IVec4 _math_ivec4_from_cglm(ivec4 const source) {
    IVec4 const self = { (ISize) source[0], (ISize) source[1], (ISize) source[2], (ISize) source[3] };

    return self;
}

static inline void _math_ivec4_raw_to_cglm(ISize const *const source, ivec4 dest) {
    dest[0] = (int) source[0];
    dest[1] = (int) source[1];
    dest[2] = (int) source[2];
    dest[3] = (int) source[3];
}

static inline void _math_ivec4_raw_from_cglm(ivec4 const source, ISize *const dest) {
    dest[0] = (ISize) source[0];
    dest[1] = (ISize) source[1];
    dest[2] = (ISize) source[2];
    dest[3] = (ISize) source[3];
}

/*==============================================================================
 * MARK: - Matrix bridges
 *
 * Column-major: outer index is the column, inner is the row; the raw flat form
 * stores column-major (index = col * rows + row).
 *============================================================================*/

static inline void _math_mat2_to_cglm(Mat2 const self, mat2 dest) {
    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 2; row++) {
            dest[col][row] = (float) self.m[col][row];
        }
    }
}

static inline Mat2 _math_mat2_from_cglm(mat2 const source) {
    Mat2 self = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 2; row++) {
            self.m[col][row] = (FSize) source[col][row];
        }
    }

    return self;
}

static inline void _math_mat2_raw_to_cglm(FSize const *const source, mat2 dest) {
    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 2; row++) {
            dest[col][row] = (float) source[col * 2 + row];
        }
    }
}

static inline void _math_mat2_raw_from_cglm(mat2 const source, FSize *const dest) {
    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 2; row++) {
            dest[col * 2 + row] = (FSize) source[col][row];
        }
    }
}

static inline void _math_mat3_to_cglm(Mat3 const self, mat3 dest) {
    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 3; row++) {
            dest[col][row] = (float) self.m[col][row];
        }
    }
}

static inline Mat3 _math_mat3_from_cglm(mat3 const source) {
    Mat3 self = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 3; row++) {
            self.m[col][row] = (FSize) source[col][row];
        }
    }

    return self;
}

static inline void _math_mat3_raw_to_cglm(FSize const *const source, mat3 dest) {
    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 3; row++) {
            dest[col][row] = (float) source[col * 3 + row];
        }
    }
}

static inline void _math_mat3_raw_from_cglm(mat3 const source, FSize *const dest) {
    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 3; row++) {
            dest[col * 3 + row] = (FSize) source[col][row];
        }
    }
}

static inline void _math_mat4_to_cglm(Mat4 const self, mat4 dest) {
    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 4; row++) {
            dest[col][row] = (float) self.m[col][row];
        }
    }
}

static inline Mat4 _math_mat4_from_cglm(mat4 const source) {
    Mat4 self = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 4; row++) {
            self.m[col][row] = (FSize) source[col][row];
        }
    }

    return self;
}

static inline void _math_mat4_raw_to_cglm(FSize const *const source, mat4 dest) {
    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 4; row++) {
            dest[col][row] = (float) source[col * 4 + row];
        }
    }
}

static inline void _math_mat4_raw_from_cglm(mat4 const source, FSize *const dest) {
    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 4; row++) {
            dest[col * 4 + row] = (FSize) source[col][row];
        }
    }
}

/*==============================================================================
 * MARK: - Non-square matrix bridges
 *
 * MatCxR stores C columns of R rows as m[C][R]; the raw flat form is column-major
 * (index = col * R + row).
 *============================================================================*/

static inline void _math_mat2x3_to_cglm(Mat2x3 const self, mat2x3 dest) {
    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 3; row++) {
            dest[col][row] = (float) self.m[col][row];
        }
    }
}

static inline Mat2x3 _math_mat2x3_from_cglm(mat2x3 const source) {
    Mat2x3 self = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 3; row++) {
            self.m[col][row] = (FSize) source[col][row];
        }
    }

    return self;
}

static inline void _math_mat2x3_raw_to_cglm(FSize const *const source, mat2x3 dest) {
    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 3; row++) {
            dest[col][row] = (float) source[col * 3 + row];
        }
    }
}

static inline void _math_mat2x3_raw_from_cglm(mat2x3 const source, FSize *const dest) {
    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 3; row++) {
            dest[col * 3 + row] = (FSize) source[col][row];
        }
    }
}

static inline void _math_mat2x4_to_cglm(Mat2x4 const self, mat2x4 dest) {
    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 4; row++) {
            dest[col][row] = (float) self.m[col][row];
        }
    }
}

static inline Mat2x4 _math_mat2x4_from_cglm(mat2x4 const source) {
    Mat2x4 self = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 4; row++) {
            self.m[col][row] = (FSize) source[col][row];
        }
    }

    return self;
}

static inline void _math_mat2x4_raw_to_cglm(FSize const *const source, mat2x4 dest) {
    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 4; row++) {
            dest[col][row] = (float) source[col * 4 + row];
        }
    }
}

static inline void _math_mat2x4_raw_from_cglm(mat2x4 const source, FSize *const dest) {
    for (USize col = 0; col < 2; col++) {
        for (USize row = 0; row < 4; row++) {
            dest[col * 4 + row] = (FSize) source[col][row];
        }
    }
}

static inline void _math_mat3x2_to_cglm(Mat3x2 const self, mat3x2 dest) {
    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 2; row++) {
            dest[col][row] = (float) self.m[col][row];
        }
    }
}

static inline Mat3x2 _math_mat3x2_from_cglm(mat3x2 const source) {
    Mat3x2 self = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 2; row++) {
            self.m[col][row] = (FSize) source[col][row];
        }
    }

    return self;
}

static inline void _math_mat3x2_raw_to_cglm(FSize const *const source, mat3x2 dest) {
    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 2; row++) {
            dest[col][row] = (float) source[col * 2 + row];
        }
    }
}

static inline void _math_mat3x2_raw_from_cglm(mat3x2 const source, FSize *const dest) {
    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 2; row++) {
            dest[col * 2 + row] = (FSize) source[col][row];
        }
    }
}

static inline void _math_mat3x4_to_cglm(Mat3x4 const self, mat3x4 dest) {
    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 4; row++) {
            dest[col][row] = (float) self.m[col][row];
        }
    }
}

static inline Mat3x4 _math_mat3x4_from_cglm(mat3x4 const source) {
    Mat3x4 self = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 4; row++) {
            self.m[col][row] = (FSize) source[col][row];
        }
    }

    return self;
}

static inline void _math_mat3x4_raw_to_cglm(FSize const *const source, mat3x4 dest) {
    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 4; row++) {
            dest[col][row] = (float) source[col * 4 + row];
        }
    }
}

static inline void _math_mat3x4_raw_from_cglm(mat3x4 const source, FSize *const dest) {
    for (USize col = 0; col < 3; col++) {
        for (USize row = 0; row < 4; row++) {
            dest[col * 4 + row] = (FSize) source[col][row];
        }
    }
}

static inline void _math_mat4x2_to_cglm(Mat4x2 const self, mat4x2 dest) {
    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 2; row++) {
            dest[col][row] = (float) self.m[col][row];
        }
    }
}

static inline Mat4x2 _math_mat4x2_from_cglm(mat4x2 const source) {
    Mat4x2 self = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 2; row++) {
            self.m[col][row] = (FSize) source[col][row];
        }
    }

    return self;
}

static inline void _math_mat4x2_raw_to_cglm(FSize const *const source, mat4x2 dest) {
    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 2; row++) {
            dest[col][row] = (float) source[col * 2 + row];
        }
    }
}

static inline void _math_mat4x2_raw_from_cglm(mat4x2 const source, FSize *const dest) {
    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 2; row++) {
            dest[col * 2 + row] = (FSize) source[col][row];
        }
    }
}

static inline void _math_mat4x3_to_cglm(Mat4x3 const self, mat4x3 dest) {
    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 3; row++) {
            dest[col][row] = (float) self.m[col][row];
        }
    }
}

static inline Mat4x3 _math_mat4x3_from_cglm(mat4x3 const source) {
    Mat4x3 self = DEFAULT_INITIALIZATION;

    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 3; row++) {
            self.m[col][row] = (FSize) source[col][row];
        }
    }

    return self;
}

static inline void _math_mat4x3_raw_to_cglm(FSize const *const source, mat4x3 dest) {
    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 3; row++) {
            dest[col][row] = (float) source[col * 3 + row];
        }
    }
}

static inline void _math_mat4x3_raw_from_cglm(mat4x3 const source, FSize *const dest) {
    for (USize col = 0; col < 4; col++) {
        for (USize row = 0; row < 3; row++) {
            dest[col * 3 + row] = (FSize) source[col][row];
        }
    }
}

/*==============================================================================
 * MARK: - Quaternion bridges
 *============================================================================*/

static inline void _math_quat_to_cglm(Quat const self, versor dest) {
    dest[0] = (float) self.x;
    dest[1] = (float) self.y;
    dest[2] = (float) self.z;
    dest[3] = (float) self.w;
}

static inline Quat _math_quat_from_cglm(versor const source) {
    Quat const self = { (FSize) source[0], (FSize) source[1], (FSize) source[2], (FSize) source[3] };

    return self;
}

static inline void _math_quat_raw_to_cglm(FSize const *const source, versor dest) {
    dest[0] = (float) source[0];
    dest[1] = (float) source[1];
    dest[2] = (float) source[2];
    dest[3] = (float) source[3];
}

static inline void _math_quat_raw_from_cglm(versor const source, FSize *const dest) {
    dest[0] = (FSize) source[0];
    dest[1] = (FSize) source[1];
    dest[2] = (FSize) source[2];
    dest[3] = (FSize) source[3];
}

/*==============================================================================
 * MARK: - Compound bridges
 *============================================================================*/

// Plane is normal xyz + distance w; cglm takes it as a vec4.
static inline void _math_plane_to_cglm(Plane const self, vec4 dest) {
    dest[0] = (float) self.x;
    dest[1] = (float) self.y;
    dest[2] = (float) self.z;
    dest[3] = (float) self.w;
}

static inline Plane _math_plane_from_cglm(vec4 const source) {
    Plane const self = { (FSize) source[0], (FSize) source[1], (FSize) source[2], (FSize) source[3] };

    return self;
}

// Box is min/max; cglm takes the pair as vec3[2].
static inline void _math_box_to_cglm(Box const self, vec3 dest[2]) {
    _math_vec3_to_cglm(self.min, dest[0]);
    _math_vec3_to_cglm(self.max, dest[1]);
}

// Aabb2d is min/max; cglm takes the pair as vec2[2].
static inline void _math_aabb2d_to_cglm(Aabb2d const self, vec2 dest[2]) {
    _math_vec2_to_cglm(self.min, dest[0]);
    _math_vec2_to_cglm(self.max, dest[1]);
}

// FrustumCorners is eight homogeneous points; cglm takes them as vec4[8].
static inline void _math_frustum_corners_to_cglm(FrustumCorners const self, vec4 dest[8]) {
    for (USize i = 0; i < 8; i += 1) {
        _math_vec4_to_cglm(self.corners[i], dest[i]);
    }
}

static inline FrustumCorners _math_frustum_corners_from_cglm(vec4 const source[8]) {
    FrustumCorners self = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 8; i += 1) {
        self.corners[i] = _math_vec4_from_cglm(source[i]);
    }

    return self;
}

// FrustumPlanes is six planes; cglm takes them as vec4[6] (normal xyz, distance w).
static inline void _math_frustum_planes_to_cglm(FrustumPlanes const self, vec4 dest[6]) {
    for (USize i = 0; i < 6; i += 1) {
        _math_plane_to_cglm(self.planes[i], dest[i]);
    }
}

static inline FrustumPlanes _math_frustum_planes_from_cglm(vec4 const source[6]) {
    FrustumPlanes self = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 6; i += 1) {
        self.planes[i] = _math_plane_from_cglm(source[i]);
    }

    return self;
}

// FrustumSplitCorners is four homogeneous points; cglm writes them as vec4[4].
static inline FrustumSplitCorners _math_frustum_split_corners_from_cglm(vec4 const source[4]) {
    FrustumSplitCorners self = DEFAULT_INITIALIZATION;

    for (USize i = 0; i < 4; i += 1) {
        self.corners[i] = _math_vec4_from_cglm(source[i]);
    }

    return self;
}

// Sphere is center + radius; cglm takes it as a vec4.
static inline void _math_sphere_to_cglm(Sphere const self, vec4 dest) {
    dest[0] = (float) self.x;
    dest[1] = (float) self.y;
    dest[2] = (float) self.z;
    dest[3] = (float) self.r;
}

static inline Sphere _math_sphere_from_cglm(vec4 const source) {
    Sphere const self = { (FSize) source[0], (FSize) source[1], (FSize) source[2], (FSize) source[3] };

    return self;
}

// Circle is center + radius; cglm takes it as a vec3.
static inline void _math_circle_to_cglm(Circle const self, vec3 dest) {
    dest[0] = (float) self.x;
    dest[1] = (float) self.y;
    dest[2] = (float) self.r;
}

// Ray is origin + direction; cglm takes them as two vec3.
static inline void _math_ray_to_cglm(Ray const self, vec3 origin, vec3 direction) {
    _math_vec3_to_cglm(self.origin, origin);
    _math_vec3_to_cglm(self.direction, direction);
}

#endif // MATH_TYPES_H