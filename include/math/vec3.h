/*
 * vec3.h - 3D vector operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_vec3_* API: construction, comparison,
 *     arithmetic, fused accumulate/subtract, geometry (dot/cross/norm/distance/
 *     angle/proj), interpolation, stepping, clamping, rounding, reduction,
 *     validity predicates, rotation (axis + mat3/mat4), reflect/refract
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Vec3 (or an FSize/bool for scalar/boolean results)
 *
 * Usage Examples:
 *   @code
 *   Vec3 const a   = { 1.0, 2.0, 3.0 };
 *   Vec3 const b   = { 4.0, 5.0, 6.0 };
 *   Vec3 const sum = math_vec3_add_2(a, b);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate - except
 *     make_2, the documented array constructor, which validates its raw source pointer.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper converts FSize<->float at the boundary and calls a compiled
 *     glmc_* routine. cglm's vec3 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Vec3/Vec4/Mat3/Mat4 types, the raw<->cglm bridges,
 *     cglm, and the error/tracing macros.
 *
 * See vec3.c for implementation details.
 */

#ifndef MATH_VEC3_H
#define MATH_VEC3_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* Swizzle masks for math_vec3_swizzle_*: cglm's values (its GLM_XXX / GLM_ZYX lane orders) built with
 * GLM_SHUFFLE3 from common.h, which the compiled-API include path always provides; cglm's own named
 * masks live in its inline vec3.h, which that path does not. Callers never spell GLM_*; MATH_SWIZZLE3
 * builds any of the 27 permutations from the source index of each output lane, in output order;
 * every argument must be 0..2 - GLM_SHUFFLE shifts without masking, so a larger value corrupts the
 * next lane's index instead of failing. */
#define MATH_SWIZZLE3(first, second, third) GLM_SHUFFLE3(third, second, first)
#define MATH_SWIZZLE_XXX GLM_SHUFFLE3(0, 0, 0)
#define MATH_SWIZZLE_YYY GLM_SHUFFLE3(1, 1, 1)
#define MATH_SWIZZLE_ZYX GLM_SHUFFLE3(0, 1, 2)
#define MATH_SWIZZLE_ZZZ GLM_SHUFFLE3(2, 2, 2)

/*==============================================================================
 * MARK: - Vec3 API
 *
 * Raw (_1) variants read and write 3 contiguous FSize; the struct (_2) variants
 * read and return a Vec3 value. Accumulator ops (addadd, muladd, ...) take the
 * current destination value as an input and produce the accumulated result.
 *============================================================================*/

/**
 * @brief Component-wise absolute value of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_abs_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise absolute value of a 3D vector.
 * @param v Source vector.
 * @return Absolute-value Vec3.
 */
Vec3 math_vec3_abs_2(Vec3 const v);

/**
 * @brief Add two raw 3D vectors.
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_add_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the sum of two 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Sum Vec3.
 */
Vec3 math_vec3_add_2(Vec3 const a, Vec3 const b);

/**
 * @brief Accumulate (a + b) into a raw destination: dest += a + b.
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_addadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + a + b for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_addadd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Add a scalar to every component of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @param s Scalar addend.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_adds_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 3D vector with a scalar added to every component.
 * @param v Source vector.
 * @param s Scalar addend.
 * @return Result Vec3.
 */
Vec3 math_vec3_adds_2(Vec3 const v, FSize const s);

/**
 * @brief Subtract (a + b) from a raw destination: dest -= a + b.
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_addsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - (a + b) for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_addsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Angle in radians between two raw 3D vectors.
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @return Angle in radians as FSize.
 */
FSize math_vec3_angle_1(FSize const *const a, FSize const *const b);

/**
 * @brief Angle in radians between two 3D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Angle in radians as FSize.
 */
FSize math_vec3_angle_2(Vec3 const a, Vec3 const b);

/**
 * @brief Fill a raw 3D destination with a single broadcast scalar value.
 * @param val Scalar written to every component.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_broadcast_1(FSize const val, FSize *const dest);

/**
 * @brief Return a 3D vector with every component set to a broadcast scalar.
 * @param val Scalar written to every component.
 * @return Broadcast Vec3.
 */
Vec3 math_vec3_broadcast_2(FSize const val);

/**
 * @brief Midpoint of two raw 3D vectors.
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_center_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the midpoint of two 3D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Midpoint Vec3.
 */
Vec3 math_vec3_center_2(Vec3 const a, Vec3 const b);

/**
 * @brief Clamp every component of a raw 3D vector into [minval, maxval].
 * @param v Raw source vector (3 contiguous FSize).
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_clamp_1(FSize const *const v, FSize const minval, FSize const maxval, FSize *const dest);

/**
 * @brief Return a 3D vector with every component clamped into [minval, maxval].
 * @param v Source vector.
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @return Clamped Vec3.
 */
Vec3 math_vec3_clamp_2(Vec3 const v, FSize const minval, FSize const maxval);

/**
 * @brief Copy a raw 3D vector.
 * @param a Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_copy_1(FSize const *const a, FSize *const dest);

/**
 * @brief Return a copy of a 3D vector.
 * @param a Source vector.
 * @return Copied Vec3.
 */
Vec3 math_vec3_copy_2(Vec3 const a);

/**
 * @brief Cross product of two raw 3D vectors (a x b).
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_cross_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the cross product of two 3D vectors (a x b).
 * @param a First vector.
 * @param b Second vector.
 * @return Cross-product Vec3.
 */
Vec3 math_vec3_cross_2(Vec3 const a, Vec3 const b);

/**
 * @brief Normalized cross product of two raw 3D vectors (unit a x b).
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_crossn_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the normalized cross product of two 3D vectors (unit a x b).
 * @param a First vector.
 * @param b Second vector.
 * @return Unit cross-product Vec3.
 */
Vec3 math_vec3_crossn_2(Vec3 const a, Vec3 const b);

/**
 * @brief Euclidean distance between two raw 3D vectors.
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @return Distance as FSize.
 */
FSize math_vec3_distance_1(FSize const *const a, FSize const *const b);

/**
 * @brief Euclidean distance between two 3D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Distance as FSize.
 */
FSize math_vec3_distance_2(Vec3 const a, Vec3 const b);

/**
 * @brief Squared Euclidean distance between two raw 3D vectors.
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @return Squared distance as FSize.
 */
FSize math_vec3_distance2_1(FSize const *const a, FSize const *const b);

/**
 * @brief Squared Euclidean distance between two 3D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Squared distance as FSize.
 */
FSize math_vec3_distance2_2(Vec3 const a, Vec3 const b);

/**
 * @brief Component-wise division of two raw 3D vectors (a / b).
 * @param a Raw numerator (3 contiguous FSize).
 * @param b Raw denominator (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_div_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise division of two 3D vectors (a / b).
 * @param a Numerator.
 * @param b Denominator.
 * @return Quotient Vec3.
 */
Vec3 math_vec3_div_2(Vec3 const a, Vec3 const b);

/**
 * @brief Divide every component of a raw 3D vector by a scalar.
 * @param v Raw source vector (3 contiguous FSize).
 * @param s Scalar divisor.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_divs_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 3D vector with every component divided by a scalar.
 * @param v Source vector.
 * @param s Scalar divisor.
 * @return Result Vec3.
 */
Vec3 math_vec3_divs_2(Vec3 const v, FSize const s);

/**
 * @brief Dot product of two raw 3D vectors.
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @return Dot product as FSize.
 */
FSize math_vec3_dot_1(FSize const *const a, FSize const *const b);

/**
 * @brief Dot product of two 3D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Dot product as FSize.
 */
FSize math_vec3_dot_2(Vec3 const a, Vec3 const b);

/**
 * @brief Test whether every component of a raw 3D vector equals a scalar.
 * @param v Raw source vector (3 contiguous FSize).
 * @param val Scalar to compare against.
 * @return true when every component equals val.
 */
bool math_vec3_eq_1(FSize const *const v, FSize const val);

/**
 * @brief Test whether every component of a 3D vector equals a scalar.
 * @param v Source vector.
 * @param val Scalar to compare against.
 * @return true when every component equals val.
 */
bool math_vec3_eq_2(Vec3 const v, FSize const val);

/**
 * @brief Test whether all components of a raw 3D vector are equal to each other.
 * @param v Raw source vector (3 contiguous FSize).
 * @return true when all components are equal.
 */
bool math_vec3_eq_all_1(FSize const *const v);

/**
 * @brief Test whether all components of a 3D vector are equal to each other.
 * @param v Source vector.
 * @return true when all components are equal.
 */
bool math_vec3_eq_all_2(Vec3 const v);

/**
 * @brief Test whether every component of a raw 3D vector equals a scalar
 *        within an epsilon.
 * @param v Raw source vector (3 contiguous FSize).
 * @param val Scalar to compare against.
 * @return true when every component equals val within epsilon.
 */
bool math_vec3_eq_eps_1(FSize const *const v, FSize const val);

/**
 * @brief Test whether every component of a 3D vector equals a scalar within
 *        an epsilon.
 * @param v Source vector.
 * @param val Scalar to compare against.
 * @return true when every component equals val within epsilon.
 */
bool math_vec3_eq_eps_2(Vec3 const v, FSize const val);

/**
 * @brief Test two raw 3D vectors for exact equality.
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @return true when all components are equal.
 */
bool math_vec3_eqv_1(FSize const *const a, FSize const *const b);

/**
 * @brief Test two 3D vectors for exact equality.
 * @param a First vector.
 * @param b Second vector.
 * @return true when all components are equal.
 */
bool math_vec3_eqv_2(Vec3 const a, Vec3 const b);

/**
 * @brief Test two raw 3D vectors for equality within an epsilon.
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @return true when all components are equal within epsilon.
 */
bool math_vec3_eqv_eps_1(FSize const *const a, FSize const *const b);

/**
 * @brief Test two 3D vectors for equality within an epsilon.
 * @param a First vector.
 * @param b Second vector.
 * @return true when all components are equal within epsilon.
 */
bool math_vec3_eqv_eps_2(Vec3 const a, Vec3 const b);

/**
 * @brief Reorient a raw normal to face against an incident vector.
 * @param n Raw normal vector (3 contiguous FSize).
 * @param v Raw incident vector (3 contiguous FSize).
 * @param nref Raw reference normal (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_faceforward_1(FSize const *const n, FSize const *const v, FSize const *const nref, FSize *const dest);

/**
 * @brief Return a normal reoriented to face against an incident vector.
 * @param n Normal vector.
 * @param v Incident vector.
 * @param nref Reference normal.
 * @return Reoriented Vec3.
 */
Vec3 math_vec3_faceforward_2(Vec3 const n, Vec3 const v, Vec3 const nref);

/**
 * @brief Fill a raw 3D destination with a single scalar value.
 * @param val Scalar written to every component.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_fill_1(FSize const val, FSize *const dest);

/**
 * @brief Return a 3D vector with every component set to a scalar value.
 * @param val Scalar written to every component.
 * @return Filled Vec3.
 */
Vec3 math_vec3_fill_2(FSize const val);

/**
 * @brief Component-wise floor of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_floor_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise floor of a 3D vector.
 * @param v Source vector.
 * @return Floored Vec3.
 */
Vec3 math_vec3_floor_2(Vec3 const v);

/**
 * @brief Component-wise fractional part of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_fract_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise fractional part of a 3D vector.
 * @param v Source vector.
 * @return Fractional Vec3.
 */
Vec3 math_vec3_fract_2(Vec3 const v);

/**
 * @brief Horizontal sum of the components of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @return Sum of components as FSize.
 */
FSize math_vec3_hadd_1(FSize const *const v);

/**
 * @brief Horizontal sum of the components of a 3D vector.
 * @param v Source vector.
 * @return Sum of components as FSize.
 */
FSize math_vec3_hadd_2(Vec3 const v);

/**
 * @brief Test whether any component of a raw 3D vector is infinite.
 * @param v Raw source vector (3 contiguous FSize).
 * @return true when any component is +/-inf.
 */
bool math_vec3_isinf_1(FSize const *const v);

/**
 * @brief Test whether any component of a 3D vector is infinite.
 * @param v Source vector.
 * @return true when any component is +/-inf.
 */
bool math_vec3_isinf_2(Vec3 const v);

/**
 * @brief Test whether any component of a raw 3D vector is NaN.
 * @param v Raw source vector (3 contiguous FSize).
 * @return true when any component is NaN.
 */
bool math_vec3_isnan_1(FSize const *const v);

/**
 * @brief Test whether any component of a 3D vector is NaN.
 * @param v Source vector.
 * @return true when any component is NaN.
 */
bool math_vec3_isnan_2(Vec3 const v);

/**
 * @brief Test whether a raw 3D vector is finite (no NaN or inf component).
 * @param v Raw source vector (3 contiguous FSize).
 * @return true when every component is finite.
 */
bool math_vec3_isvalid_1(FSize const *const v);

/**
 * @brief Test whether a 3D vector is finite (no NaN or inf component).
 * @param v Source vector.
 * @return true when every component is finite.
 */
bool math_vec3_isvalid_2(Vec3 const v);

/**
 * @brief Linearly interpolate between two raw 3D vectors.
 * @param from Raw start vector (3 contiguous FSize).
 * @param to Raw end vector (3 contiguous FSize).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the linear interpolation between two 3D vectors.
 * @param from Start vector.
 * @param to End vector.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated Vec3.
 */
Vec3 math_vec3_lerp_2(Vec3 const from, Vec3 const to, FSize const t);

/**
 * @brief Clamped linear interpolation between two raw 3D vectors (t in [0, 1]).
 * @param from Raw start vector (3 contiguous FSize).
 * @param to Raw end vector (3 contiguous FSize).
 * @param t Interpolation factor, clamped into [0, 1].
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_lerpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the clamped linear interpolation between two 3D vectors.
 * @param from Start vector.
 * @param to End vector.
 * @param t Interpolation factor, clamped into [0, 1].
 * @return Interpolated Vec3.
 */
Vec3 math_vec3_lerpc_2(Vec3 const from, Vec3 const to, FSize const t);

/**
 * @brief Construct a raw 3D vector from a raw FSize source array.
 * @param src Raw source array (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 3D vector from a raw FSize source array.
 * @param src Raw source array (3 contiguous FSize).
 * @return Constructed Vec3.
 */
Vec3 math_vec3_make_2(FSize const *const src);

/**
 * @brief Largest component of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @return Maximum component as FSize.
 */
FSize math_vec3_max_1(FSize const *const v);

/**
 * @brief Largest component of a 3D vector.
 * @param v Source vector.
 * @return Maximum component as FSize.
 */
FSize math_vec3_max_2(Vec3 const v);

/**
 * @brief Accumulate the component-wise maximum of a and b into a raw destination:
 *        dest += max(a, b).
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_maxadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator + max(a, b) for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_maxadd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Subtract the component-wise maximum of a and b from a raw destination:
 *        dest -= max(a, b).
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_maxsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator - max(a, b) for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_maxsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Component-wise maximum of two raw 3D vectors.
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_maxv_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise maximum of two 3D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise maximum Vec3.
 */
Vec3 math_vec3_maxv_2(Vec3 const a, Vec3 const b);

/**
 * @brief Smallest component of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @return Minimum component as FSize.
 */
FSize math_vec3_min_1(FSize const *const v);

/**
 * @brief Smallest component of a 3D vector.
 * @param v Source vector.
 * @return Minimum component as FSize.
 */
FSize math_vec3_min_2(Vec3 const v);

/**
 * @brief Accumulate the component-wise minimum of a and b into a raw destination:
 *        dest += min(a, b).
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_minadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator + min(a, b) for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_minadd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Subtract the component-wise minimum of a and b from a raw destination:
 *        dest -= min(a, b).
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_minsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator - min(a, b) for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_minsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Component-wise minimum of two raw 3D vectors.
 * @param a Raw first vector (3 contiguous FSize).
 * @param b Raw second vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_minv_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise minimum of two 3D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise minimum Vec3.
 */
Vec3 math_vec3_minv_2(Vec3 const a, Vec3 const b);

/**
 * @brief Component-wise modulo of a raw 3D vector by a scalar.
 * @param v Raw source vector (3 contiguous FSize).
 * @param s Scalar divisor.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_mods_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return the component-wise modulo of a 3D vector by a scalar.
 * @param v Source vector.
 * @param s Scalar divisor.
 * @return Result Vec3.
 */
Vec3 math_vec3_mods_2(Vec3 const v, FSize const s);

/**
 * @brief Component-wise multiplication of two raw 3D vectors.
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise multiplication of two 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Product Vec3.
 */
Vec3 math_vec3_mul_2(Vec3 const a, Vec3 const b);

/**
 * @brief Accumulate (a * b) into a raw destination: dest += a * b.
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_muladd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + a * b for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_muladd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Accumulate (a * s) into a raw destination: dest += a * s.
 * @param a Raw source vector (3 contiguous FSize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_muladds_1(FSize const *const a, FSize const s, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + a * s for a 3D vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_muladds_2(Vec3 const a, FSize const s, Vec3 const accumulator);

/**
 * @brief Subtract (a * b) from a raw destination: dest -= a * b.
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_mulsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - a * b for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_mulsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Subtract (a * s) from a raw destination: dest -= a * s.
 * @param a Raw source vector (3 contiguous FSize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_mulsubs_1(FSize const *const a, FSize const s, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - a * s for a 3D vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_mulsubs_2(Vec3 const a, FSize const s, Vec3 const accumulator);

/**
 * @brief Component-wise multiplication of two raw 3D vectors (mulv alias).
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_mulv_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise multiplication of two 3D vectors (mulv alias).
 * @param a Left vector.
 * @param b Right vector.
 * @return Product Vec3.
 */
Vec3 math_vec3_mulv_2(Vec3 const a, Vec3 const b);

/**
 * @brief Negate a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_negate_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the negation of a 3D vector.
 * @param v Source vector.
 * @return Negated Vec3.
 */
Vec3 math_vec3_negate_2(Vec3 const v);

/**
 * @brief Euclidean length (norm) of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @return Norm as FSize.
 */
FSize math_vec3_norm_1(FSize const *const v);

/**
 * @brief Euclidean length (norm) of a 3D vector.
 * @param v Source vector.
 * @return Norm as FSize.
 */
FSize math_vec3_norm_2(Vec3 const v);

/**
 * @brief Squared Euclidean length (norm) of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @return Squared norm as FSize.
 */
FSize math_vec3_norm2_1(FSize const *const v);

/**
 * @brief Squared Euclidean length (norm) of a 3D vector.
 * @param v Source vector.
 * @return Squared norm as FSize.
 */
FSize math_vec3_norm2_2(Vec3 const v);

/**
 * @brief Infinity norm (largest absolute component) of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @return Infinity norm as FSize.
 */
FSize math_vec3_norm_inf_1(FSize const *const v);

/**
 * @brief Infinity norm (largest absolute component) of a 3D vector.
 * @param v Source vector.
 * @return Infinity norm as FSize.
 */
FSize math_vec3_norm_inf_2(Vec3 const v);

/**
 * @brief L1 norm (sum of absolute components) of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @return L1 norm as FSize.
 */
FSize math_vec3_norm_one_1(FSize const *const v);

/**
 * @brief L1 norm (sum of absolute components) of a 3D vector.
 * @param v Source vector.
 * @return L1 norm as FSize.
 */
FSize math_vec3_norm_one_2(Vec3 const v);

/**
 * @brief Normalize a raw 3D vector (zero vector maps to zero).
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_normalize_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the normalized 3D vector (zero vector maps to zero).
 * @param v Source vector.
 * @return Unit Vec3.
 */
Vec3 math_vec3_normalize_2(Vec3 const v);

/**
 * @brief Fill a raw 3D destination with ones.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_one_1(FSize *const dest);

/**
 * @brief Return a 3D vector of ones.
 * @return Vec3 with every component set to 1.
 */
Vec3 math_vec3_one_2(void);

/**
 * @brief Build a raw 3D vector orthogonal to a raw source vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_ortho_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return a 3D vector orthogonal to a source vector.
 * @param v Source vector.
 * @return Orthogonal Vec3.
 */
Vec3 math_vec3_ortho_2(Vec3 const v);

/**
 * @brief Project a raw 3D vector a onto a raw 3D vector b.
 * @param a Raw vector to project (3 contiguous FSize).
 * @param b Raw vector to project onto (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_proj_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the projection of a 3D vector a onto a 3D vector b.
 * @param a Vector to project.
 * @param b Vector to project onto.
 * @return Projection Vec3.
 */
Vec3 math_vec3_proj_2(Vec3 const a, Vec3 const b);

/**
 * @brief Reflect a raw incident vector about a raw normal.
 * @param v Raw incident vector (3 contiguous FSize).
 * @param n Raw normalized normal (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_reflect_1(FSize const *const v, FSize const *const n, FSize *const dest);

/**
 * @brief Return the reflection of an incident vector about a normal.
 * @param v Incident vector.
 * @param n Normalized normal.
 * @return Reflected Vec3.
 */
Vec3 math_vec3_reflect_2(Vec3 const v, Vec3 const n);

/**
 * @brief Refract a raw incident vector through a raw normal (Snell's law).
 * @param v Raw normalized incident vector (3 contiguous FSize).
 * @param n Raw normalized normal (3 contiguous FSize).
 * @param eta Ratio of indices of refraction (incident / transmitted); NaN, non-finite or
 *        not > 0 AFTER the float conversion (so an F64 past float range too) is refused
 *        (false, zeroed dest) - cglm would report a NaN vector as a success.
 * @param dest Destination of 3 contiguous FSize; zeroed on total internal reflection.
 * @return true when refraction occurs; false on total internal reflection.
 */
bool math_vec3_refract_1(FSize const *const v, FSize const *const n, FSize const eta, FSize *const dest);

/**
 * @brief Refract an incident vector through a normal (Snell's law).
 * @param v Normalized incident vector.
 * @param n Normalized normal.
 * @param eta Ratio of indices of refraction (incident / transmitted); NaN, non-finite or
 *        not > 0 after the float conversion (the F64 is bounded to float range before the
 *        cast, so a value past float range too) is refused to { false, zero }.
 * @return Vec3Refraction: refracted, and the refracted vector v when it did (zero on total
 *        internal reflection).
 */
Vec3Refraction math_vec3_refract_2(Vec3 const v, Vec3 const n, FSize const eta);

/**
 * @brief Rotate a raw 3D vector about a raw axis by an angle (Rodrigues).
 * @param v Raw source vector (3 contiguous FSize).
 * @param angle Rotation angle in radians.
 * @param axis Raw normalized rotation axis (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_rotate_1(FSize const *const v, FSize const angle, FSize const *const axis, FSize *const dest);

/**
 * @brief Return a 3D vector rotated about an axis by an angle (Rodrigues).
 * @param v Source vector.
 * @param angle Rotation angle in radians.
 * @param axis Normalized rotation axis.
 * @return Rotated Vec3.
 */
Vec3 math_vec3_rotate_2(Vec3 const v, FSize const angle, Vec3 const axis);

/**
 * @brief Rotate a raw 3D vector by a raw column-major 3x3 matrix.
 * @param m Raw 3x3 matrix (9 contiguous FSize, column-major).
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_rotate_m3_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return a 3D vector rotated by a column-major 3x3 matrix.
 * @param m 3x3 rotation matrix.
 * @param v Source vector.
 * @return Rotated Vec3.
 */
Vec3 math_vec3_rotate_m3_2(Mat3 const m, Vec3 const v);

/**
 * @brief Rotate a raw 3D vector by a raw column-major 4x4 matrix.
 * @param m Raw 4x4 matrix (16 contiguous FSize, column-major).
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_rotate_m4_1(FSize const *const m, FSize const *const v, FSize *const dest);

/**
 * @brief Return a 3D vector rotated by a column-major 4x4 matrix.
 * @param m 4x4 rotation matrix.
 * @param v Source vector.
 * @return Rotated Vec3.
 */
Vec3 math_vec3_rotate_m4_2(Mat4 const m, Vec3 const v);

/**
 * @brief Scale a raw 3D vector by a scalar.
 * @param v Raw source vector (3 contiguous FSize).
 * @param s Scalar factor.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_scale_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 3D vector scaled by a scalar.
 * @param v Source vector.
 * @param s Scalar factor.
 * @return Scaled Vec3.
 */
Vec3 math_vec3_scale_2(Vec3 const v, FSize const s);

/**
 * @brief Scale a raw 3D vector to a given length.
 * @param v Raw source vector (3 contiguous FSize).
 * @param s Target length.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_scale_as_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 3D vector scaled to a given length.
 * @param v Source vector.
 * @param s Target length.
 * @return Scaled Vec3.
 */
Vec3 math_vec3_scale_as_2(Vec3 const v, FSize const s);

/**
 * @brief Component-wise sign of a raw 3D vector (-1, 0, or 1 per component).
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_sign_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise sign of a 3D vector (-1, 0, or 1).
 * @param v Source vector.
 * @return Sign Vec3.
 */
Vec3 math_vec3_sign_2(Vec3 const v);

/**
 * @brief Smooth (Hermite) interpolation between two raw 3D vectors.
 * @param from Raw start vector (3 contiguous FSize).
 * @param to Raw end vector (3 contiguous FSize).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_smoothinterp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the smooth (Hermite) interpolation between two 3D vectors.
 * @param from Start vector.
 * @param to End vector.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated Vec3.
 */
Vec3 math_vec3_smoothinterp_2(Vec3 const from, Vec3 const to, FSize const t);

/**
 * @brief Clamped smooth (Hermite) interpolation between two raw 3D vectors.
 * @param from Raw start vector (3 contiguous FSize).
 * @param to Raw end vector (3 contiguous FSize).
 * @param t Interpolation factor, clamped into [0, 1].
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_smoothinterpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the clamped smooth (Hermite) interpolation between two 3D vectors.
 * @param from Start vector.
 * @param to End vector.
 * @param t Interpolation factor, clamped into [0, 1].
 * @return Interpolated Vec3.
 */
Vec3 math_vec3_smoothinterpc_2(Vec3 const from, Vec3 const to, FSize const t);

/**
 * @brief Component-wise smoothstep of a raw 3D vector between raw edge vectors.
 * @param edge0 Raw lower edge vector (3 contiguous FSize).
 * @param edge1 Raw upper edge vector (3 contiguous FSize).
 * @param x Raw value vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_smoothstep_1(FSize const *const edge0, FSize const *const edge1, FSize const *const x, FSize *const dest);

/**
 * @brief Return the component-wise smoothstep of a 3D vector between edge vectors.
 * @param edge0 Lower edge vector.
 * @param edge1 Upper edge vector.
 * @param x Value vector.
 * @return Smoothstep result Vec3.
 */
Vec3 math_vec3_smoothstep_2(Vec3 const edge0, Vec3 const edge1, Vec3 const x);

/**
 * @brief Component-wise smoothstep of a raw 3D vector between scalar edges.
 * @param edge0 Lower scalar edge.
 * @param edge1 Upper scalar edge.
 * @param x Raw value vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_smoothstep_uni_1(FSize const edge0, FSize const edge1, FSize const *const x, FSize *const dest);

/**
 * @brief Return the component-wise smoothstep of a 3D vector between scalar edges.
 * @param edge0 Lower scalar edge.
 * @param edge1 Upper scalar edge.
 * @param x Value vector.
 * @return Smoothstep result Vec3.
 */
Vec3 math_vec3_smoothstep_uni_2(FSize const edge0, FSize const edge1, Vec3 const x);

/**
 * @brief Component-wise square root of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_sqrt_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise square root of a 3D vector.
 * @param v Source vector.
 * @return Square-root Vec3.
 */
Vec3 math_vec3_sqrt_2(Vec3 const v);

/**
 * @brief Component-wise step of a raw 3D vector against a raw edge vector.
 * @param edge Raw threshold vector (3 contiguous FSize).
 * @param x Raw value vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_step_1(FSize const *const edge, FSize const *const x, FSize *const dest);

/**
 * @brief Return the component-wise step of a 3D vector against an edge vector.
 * @param edge Threshold vector.
 * @param x Value vector.
 * @return Step result Vec3 (0 or 1 per component).
 */
Vec3 math_vec3_step_2(Vec3 const edge, Vec3 const x);

/**
 * @brief Step a raw 3D value vector against a scalar edge.
 * @param edge Scalar threshold.
 * @param x Raw value vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_steps_1(FSize const edge, FSize const *const x, FSize *const dest);

/**
 * @brief Return the step of a 3D value vector against a scalar edge.
 * @param edge Scalar threshold.
 * @param x Value vector.
 * @return Step result Vec3 (0 or 1 per component).
 */
Vec3 math_vec3_steps_2(FSize const edge, Vec3 const x);

/**
 * @brief Step a scalar value against a raw 3D edge vector.
 * @param edge Raw threshold vector (3 contiguous FSize).
 * @param x Scalar value to test.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_stepr_1(FSize const *const edge, FSize const x, FSize *const dest);

/**
 * @brief Return the step of a scalar value against a 3D edge vector.
 * @param edge Threshold vector.
 * @param x Scalar value to test.
 * @return Step result Vec3 (0 or 1 per component).
 */
Vec3 math_vec3_stepr_2(Vec3 const edge, FSize const x);

/**
 * @brief Subtract two raw 3D vectors (a - b).
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_sub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the difference of two 3D vectors (a - b).
 * @param a Left vector.
 * @param b Right vector.
 * @return Difference Vec3.
 */
Vec3 math_vec3_sub_2(Vec3 const a, Vec3 const b);

/**
 * @brief Accumulate (a - b) into a raw destination: dest += a - b.
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_subadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + (a - b) for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_subadd_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Subtract a scalar from every component of a raw 3D vector.
 * @param v Raw source vector (3 contiguous FSize).
 * @param s Scalar subtrahend.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_subs_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 3D vector with a scalar subtracted from every component.
 * @param v Source vector.
 * @param s Scalar subtrahend.
 * @return Result Vec3.
 */
Vec3 math_vec3_subs_2(Vec3 const v, FSize const s);

/**
 * @brief Subtract (a - b) from a raw destination: dest -= a - b.
 * @param a Raw left vector (3 contiguous FSize).
 * @param b Raw right vector (3 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous FSize).
 */
void math_vec3_subsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - (a - b) for 3D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec3.
 */
Vec3 math_vec3_subsub_2(Vec3 const a, Vec3 const b, Vec3 const accumulator);

/**
 * @brief Swizzle a raw 3D vector by a component mask.
 * @param v Raw source vector (3 contiguous FSize).
 * @param mask A MATH_SWIZZLE_* mask for THIS arity; a mask whose first 3 lanes hold an index
 *        at or past the arity (another vector size's mask) is refused and the result is
 *        zeroed. Lanes past the arity are never read, so a wider mask with in-range low
 *        lanes is accepted.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_swizzle_1(FSize const *const v, ISize const mask, FSize *const dest);

/**
 * @brief Return a 3D vector swizzled by a component mask.
 * @param v Source vector.
 * @param mask A MATH_SWIZZLE_* mask for THIS arity; a mask whose first 3 lanes hold an index
 *        at or past the arity (another vector size's mask) is refused and the result is
 *        zeroed. Lanes past the arity are never read, so a wider mask with in-range low
 *        lanes is accepted.
 * @return Swizzled Vec3.
 */
Vec3 math_vec3_swizzle_2(Vec3 const v, ISize const mask);

/**
 * @brief Construct a raw 3D vector from the first 3 components of a raw vec4.
 * @param v Raw source array (4 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_vec3_1(FSize const *const v, FSize *const dest);

/**
 * @brief Construct a 3D vector from the first 3 components of a 4D vector.
 * @param v Source 4D vector.
 * @return Constructed Vec3.
 */
Vec3 math_vec3_vec3_2(Vec4 const v);

/**
 * @brief Fill a raw 3D destination with zeros.
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec3_zero_1(FSize *const dest);

/**
 * @brief Return a 3D zero vector.
 * @return Vec3 with every component set to 0.
 */
Vec3 math_vec3_zero_2(void);

#endif // MATH_VEC3_H