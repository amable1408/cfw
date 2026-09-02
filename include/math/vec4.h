/*
 * vec4.h - 4D vector operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_vec4_* API: construction, comparison,
 *     validity tests, arithmetic, fused accumulate/subtract, geometry
 *     (dot/norm/distance), interpolation, smoothstep, stepping, clamping,
 *     rounding, reflect/refract, swizzle
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Vec4 (or an FSize/bool for scalar/boolean results)
 *
 * Usage Examples:
 *   @code
 *   Vec4 const a   = { 1.0, 2.0, 3.0, 4.0 };
 *   Vec4 const b   = { 5.0, 6.0, 7.0, 8.0 };
 *   Vec4 const sum = math_vec4_add_2(a, b);
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
 *     glmc_* routine; cglm's vec4 paths are SIMD-accelerated (SSE2/AVX) where the
 *     build enables them.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Vec4 type, the raw<->cglm bridges, cglm, and the
 *     error/tracing macros.
 *
 * See vec4.c for implementation details.
 */

#ifndef MATH_VEC4_H
#define MATH_VEC4_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* Swizzle masks for math_vec4_swizzle_*: cglm's values (its GLM_XXXX / GLM_WZYX lane orders) built
 * with GLM_SHUFFLE4 from common.h, which the compiled-API include path always provides; cglm's own
 * named masks live in its inline vec4.h, which that path does not. Callers never spell GLM_*;
 * MATH_SWIZZLE4 builds any of the 256 permutations from the source index of each output lane, in
 * output order; every argument must be 0..3 - GLM_SHUFFLE shifts without masking, so a larger
 * value corrupts the next lane's index instead of failing. */
#define MATH_SWIZZLE4(first, second, third, fourth) GLM_SHUFFLE4(fourth, third, second, first)
#define MATH_SWIZZLE_WWWW GLM_SHUFFLE4(3, 3, 3, 3)
#define MATH_SWIZZLE_WZYX GLM_SHUFFLE4(0, 1, 2, 3)
#define MATH_SWIZZLE_XXXX GLM_SHUFFLE4(0, 0, 0, 0)
#define MATH_SWIZZLE_YYYY GLM_SHUFFLE4(1, 1, 1, 1)
#define MATH_SWIZZLE_ZZZZ GLM_SHUFFLE4(2, 2, 2, 2)

/*==============================================================================
 * MARK: - Vec4 API
 *
 * Raw (_1) variants read and write 4 contiguous FSize; the struct (_2) variants
 * read and return a Vec4 value. Accumulator ops (addadd, muladd, ...) take the
 * current destination value as an input and produce the accumulated result.
 *============================================================================*/

/**
 * @brief Component-wise absolute value of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_abs_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise absolute value of a 4D vector.
 * @param v Source vector.
 * @return Absolute-value Vec4.
 */
Vec4 math_vec4_abs_2(Vec4 const v);

/**
 * @brief Add two raw 4D vectors.
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_add_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the sum of two 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Sum Vec4.
 */
Vec4 math_vec4_add_2(Vec4 const a, Vec4 const b);

/**
 * @brief Accumulate (a + b) into a raw destination: dest += a + b.
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_addadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + a + b for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_addadd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Add a scalar to every component of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @param s Scalar addend.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_adds_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 4D vector with a scalar added to every component.
 * @param v Source vector.
 * @param s Scalar addend.
 * @return Result Vec4.
 */
Vec4 math_vec4_adds_2(Vec4 const v, FSize const s);

/**
 * @brief Subtract (a + b) from a raw destination: dest -= a + b.
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_addsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - (a + b) for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_addsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Broadcast a scalar into a raw 4D destination (every component = val).
 * @param val Scalar written to every component.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_broadcast_1(FSize const val, FSize *const dest);

/**
 * @brief Return a 4D vector with every component set to a broadcast scalar.
 * @param val Scalar written to every component.
 * @return Broadcast Vec4.
 */
Vec4 math_vec4_broadcast_2(FSize const val);

/**
 * @brief Clamp every component of a raw 4D vector into [minval, maxval].
 * @param v Raw source vector (4 contiguous FSize).
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_clamp_1(FSize const *const v, FSize const minval, FSize const maxval, FSize *const dest);

/**
 * @brief Return a 4D vector with every component clamped into [minval, maxval].
 * @param v Source vector.
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @return Clamped Vec4.
 */
Vec4 math_vec4_clamp_2(Vec4 const v, FSize const minval, FSize const maxval);

/**
 * @brief Copy a raw 4D vector.
 * @param a Raw source vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_copy_1(FSize const *const a, FSize *const dest);

/**
 * @brief Return a copy of a 4D vector.
 * @param a Source vector.
 * @return Copied Vec4.
 */
Vec4 math_vec4_copy_2(Vec4 const a);

/**
 * @brief Copy the first three components of a raw 4D vector into a raw 3D dest.
 * @param a Raw source vector (4 contiguous FSize).
 * @param dest Destination of 3 contiguous FSize.
 */
void math_vec4_copy3_1(FSize const *const a, FSize *const dest);

/**
 * @brief Return the first three components of a 4D vector as a Vec3.
 * @param a Source vector.
 * @return Vec3 of the x, y, z components.
 */
Vec3 math_vec4_copy3_2(Vec4 const a);

/**
 * @brief Build a raw 4D vector of the cubic weights (s^3, s^2, s, 1).
 * @param s Scalar cubic parameter.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_cubic_1(FSize const s, FSize *const dest);

/**
 * @brief Return the cubic-weight 4D vector for a scalar parameter.
 * @param s Scalar cubic parameter.
 * @return Cubic-weight Vec4.
 */
Vec4 math_vec4_cubic_2(FSize const s);

/**
 * @brief Euclidean distance between two raw 4D vectors.
 * @param a Raw first vector (4 contiguous FSize).
 * @param b Raw second vector (4 contiguous FSize).
 * @return Distance as FSize.
 */
FSize math_vec4_distance_1(FSize const *const a, FSize const *const b);

/**
 * @brief Euclidean distance between two 4D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Distance as FSize.
 */
FSize math_vec4_distance_2(Vec4 const a, Vec4 const b);

/**
 * @brief Squared Euclidean distance between two raw 4D vectors.
 * @param a Raw first vector (4 contiguous FSize).
 * @param b Raw second vector (4 contiguous FSize).
 * @return Squared distance as FSize.
 */
FSize math_vec4_distance2_1(FSize const *const a, FSize const *const b);

/**
 * @brief Squared Euclidean distance between two 4D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Squared distance as FSize.
 */
FSize math_vec4_distance2_2(Vec4 const a, Vec4 const b);

/**
 * @brief Component-wise division of two raw 4D vectors (a / b).
 * @param a Raw numerator (4 contiguous FSize).
 * @param b Raw denominator (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_div_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise division of two 4D vectors (a / b).
 * @param a Numerator.
 * @param b Denominator.
 * @return Quotient Vec4.
 */
Vec4 math_vec4_div_2(Vec4 const a, Vec4 const b);

/**
 * @brief Divide every component of a raw 4D vector by a scalar.
 * @param v Raw source vector (4 contiguous FSize).
 * @param s Scalar divisor.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_divs_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 4D vector with every component divided by a scalar.
 * @param v Source vector.
 * @param s Scalar divisor.
 * @return Result Vec4.
 */
Vec4 math_vec4_divs_2(Vec4 const v, FSize const s);

/**
 * @brief Dot product of two raw 4D vectors.
 * @param a Raw first vector (4 contiguous FSize).
 * @param b Raw second vector (4 contiguous FSize).
 * @return Dot product as FSize.
 */
FSize math_vec4_dot_1(FSize const *const a, FSize const *const b);

/**
 * @brief Dot product of two 4D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Dot product as FSize.
 */
FSize math_vec4_dot_2(Vec4 const a, Vec4 const b);

/**
 * @brief Test whether every component of a raw 4D vector equals a scalar.
 * @param v Raw source vector (4 contiguous FSize).
 * @param val Scalar to compare against.
 * @return true when all components equal val.
 */
bool math_vec4_eq_1(FSize const *const v, FSize const val);

/**
 * @brief Test whether every component of a 4D vector equals a scalar.
 * @param v Source vector.
 * @param val Scalar to compare against.
 * @return true when all components equal val.
 */
bool math_vec4_eq_2(Vec4 const v, FSize const val);

/**
 * @brief Test all components of a raw 4D vector for mutual equality.
 * @param v Raw source vector (4 contiguous FSize).
 * @return true when x == y == z == w.
 */
bool math_vec4_eq_all_1(FSize const *const v);

/**
 * @brief Test all components of a 4D vector for mutual equality.
 * @param v Source vector.
 * @return true when x == y == z == w.
 */
bool math_vec4_eq_all_2(Vec4 const v);

/**
 * @brief Test whether every component of a raw 4D vector equals a scalar within epsilon.
 * @param v Raw source vector (4 contiguous FSize).
 * @param val Scalar to compare against.
 * @return true when all components approximately equal val.
 */
bool math_vec4_eq_eps_1(FSize const *const v, FSize const val);

/**
 * @brief Test whether every component of a 4D vector equals a scalar within epsilon.
 * @param v Source vector.
 * @param val Scalar to compare against.
 * @return true when all components approximately equal val.
 */
bool math_vec4_eq_eps_2(Vec4 const v, FSize const val);

/**
 * @brief Test two raw 4D vectors for exact equality.
 * @param a Raw first vector (4 contiguous FSize).
 * @param b Raw second vector (4 contiguous FSize).
 * @return true when all components are equal.
 */
bool math_vec4_eqv_1(FSize const *const a, FSize const *const b);

/**
 * @brief Test two 4D vectors for exact equality.
 * @param a First vector.
 * @param b Second vector.
 * @return true when all components are equal.
 */
bool math_vec4_eqv_2(Vec4 const a, Vec4 const b);

/**
 * @brief Test two raw 4D vectors for equality within epsilon.
 * @param a Raw first vector (4 contiguous FSize).
 * @param b Raw second vector (4 contiguous FSize).
 * @return true when all components approximately match.
 */
bool math_vec4_eqv_eps_1(FSize const *const a, FSize const *const b);

/**
 * @brief Test two 4D vectors for equality within epsilon.
 * @param a First vector.
 * @param b Second vector.
 * @return true when all components approximately match.
 */
bool math_vec4_eqv_eps_2(Vec4 const a, Vec4 const b);

/**
 * @brief Fill a raw 4D destination with a single scalar value.
 * @param val Scalar written to every component.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_fill_1(FSize const val, FSize *const dest);

/**
 * @brief Return a 4D vector with every component set to a scalar value.
 * @param val Scalar written to every component.
 * @return Filled Vec4.
 */
Vec4 math_vec4_fill_2(FSize const val);

/**
 * @brief Component-wise floor of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_floor_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise floor of a 4D vector.
 * @param v Source vector.
 * @return Floored Vec4.
 */
Vec4 math_vec4_floor_2(Vec4 const v);

/**
 * @brief Component-wise fractional part of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_fract_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise fractional part of a 4D vector.
 * @param v Source vector.
 * @return Fractional Vec4.
 */
Vec4 math_vec4_fract_2(Vec4 const v);

/**
 * @brief Horizontal add (sum of components) of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @return Sum of the components as FSize.
 */
FSize math_vec4_hadd_1(FSize const *const v);

/**
 * @brief Horizontal add (sum of components) of a 4D vector.
 * @param v Source vector.
 * @return Sum of the components as FSize.
 */
FSize math_vec4_hadd_2(Vec4 const v);

/**
 * @brief Test whether any component of a raw 4D vector is infinite.
 * @param v Raw source vector (4 contiguous FSize).
 * @return true when a component is +/- infinity.
 */
bool math_vec4_isinf_1(FSize const *const v);

/**
 * @brief Test whether any component of a 4D vector is infinite.
 * @param v Source vector.
 * @return true when a component is +/- infinity.
 */
bool math_vec4_isinf_2(Vec4 const v);

/**
 * @brief Test whether any component of a raw 4D vector is NaN.
 * @param v Raw source vector (4 contiguous FSize).
 * @return true when a component is NaN.
 */
bool math_vec4_isnan_1(FSize const *const v);

/**
 * @brief Test whether any component of a 4D vector is NaN.
 * @param v Source vector.
 * @return true when a component is NaN.
 */
bool math_vec4_isnan_2(Vec4 const v);

/**
 * @brief Test whether every component of a raw 4D vector is finite (not NaN/inf).
 * @param v Raw source vector (4 contiguous FSize).
 * @return true when the vector is valid.
 */
bool math_vec4_isvalid_1(FSize const *const v);

/**
 * @brief Test whether every component of a 4D vector is finite (not NaN/inf).
 * @param v Source vector.
 * @return true when the vector is valid.
 */
bool math_vec4_isvalid_2(Vec4 const v);

/**
 * @brief Linearly interpolate between two raw 4D vectors.
 * @param from Raw start vector (4 contiguous FSize).
 * @param to Raw end vector (4 contiguous FSize).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the linear interpolation between two 4D vectors.
 * @param from Start vector.
 * @param to End vector.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated Vec4.
 */
Vec4 math_vec4_lerp_2(Vec4 const from, Vec4 const to, FSize const t);

/**
 * @brief Clamped linear interpolation between two raw 4D vectors (t clamped to [0,1]).
 * @param from Raw start vector (4 contiguous FSize).
 * @param to Raw end vector (4 contiguous FSize).
 * @param t Interpolation factor; clamped into [0, 1].
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_lerpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the clamped linear interpolation between two 4D vectors.
 * @param from Start vector.
 * @param to End vector.
 * @param t Interpolation factor; clamped into [0, 1].
 * @return Interpolated Vec4.
 */
Vec4 math_vec4_lerpc_2(Vec4 const from, Vec4 const to, FSize const t);

/**
 * @brief Construct a raw 4D vector from a raw FSize source array.
 * @param src Raw source array (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 4D vector from a raw FSize source array.
 * @param src Raw source array (4 contiguous FSize).
 * @return Constructed Vec4.
 */
Vec4 math_vec4_make_2(FSize const *const src);

/**
 * @brief Largest single component of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @return Maximum component as FSize.
 */
FSize math_vec4_max_1(FSize const *const v);

/**
 * @brief Largest single component of a 4D vector.
 * @param v Source vector.
 * @return Maximum component as FSize.
 */
FSize math_vec4_max_2(Vec4 const v);

/**
 * @brief Accumulate the component-wise maximum of a and b into a raw destination:
 *        dest += max(a, b).
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_maxadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator + max(a, b) for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_maxadd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Subtract the component-wise maximum of a and b from a raw destination:
 *        dest -= max(a, b).
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_maxsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator - max(a, b) for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_maxsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Component-wise maximum of two raw 4D vectors.
 * @param a Raw first vector (4 contiguous FSize).
 * @param b Raw second vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_maxv_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise maximum of two 4D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise maximum Vec4.
 */
Vec4 math_vec4_maxv_2(Vec4 const a, Vec4 const b);

/**
 * @brief Smallest single component of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @return Minimum component as FSize.
 */
FSize math_vec4_min_1(FSize const *const v);

/**
 * @brief Smallest single component of a 4D vector.
 * @param v Source vector.
 * @return Minimum component as FSize.
 */
FSize math_vec4_min_2(Vec4 const v);

/**
 * @brief Accumulate the component-wise minimum of a and b into a raw destination:
 *        dest += min(a, b).
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_minadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator + min(a, b) for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_minadd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Subtract the component-wise minimum of a and b from a raw destination:
 *        dest -= min(a, b).
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_minsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator - min(a, b) for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_minsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Component-wise minimum of two raw 4D vectors.
 * @param a Raw first vector (4 contiguous FSize).
 * @param b Raw second vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_minv_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise minimum of two 4D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise minimum Vec4.
 */
Vec4 math_vec4_minv_2(Vec4 const a, Vec4 const b);

/**
 * @brief Component-wise modulo of a raw 4D vector by a scalar.
 * @param v Raw source vector (4 contiguous FSize).
 * @param s Scalar divisor.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_mods_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return the component-wise modulo of a 4D vector by a scalar.
 * @param v Source vector.
 * @param s Scalar divisor.
 * @return Result Vec4.
 */
Vec4 math_vec4_mods_2(Vec4 const v, FSize const s);

/**
 * @brief Component-wise multiplication of two raw 4D vectors.
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise multiplication of two 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Product Vec4.
 */
Vec4 math_vec4_mul_2(Vec4 const a, Vec4 const b);

/**
 * @brief Accumulate (a * b) into a raw destination: dest += a * b.
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_muladd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + a * b for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_muladd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Accumulate (a * s) into a raw destination: dest += a * s.
 * @param a Raw source vector (4 contiguous FSize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_muladds_1(FSize const *const a, FSize const s, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + a * s for a 4D vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_muladds_2(Vec4 const a, FSize const s, Vec4 const accumulator);

/**
 * @brief Subtract (a * b) from a raw destination: dest -= a * b.
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_mulsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - a * b for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_mulsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Subtract (a * s) from a raw destination: dest -= a * s.
 * @param a Raw source vector (4 contiguous FSize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_mulsubs_1(FSize const *const a, FSize const s, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - a * s for a 4D vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_mulsubs_2(Vec4 const a, FSize const s, Vec4 const accumulator);

/**
 * @brief Component-wise multiplication of two raw 4D vectors (ext variant).
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_mulv_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise multiplication of two 4D vectors (ext variant).
 * @param a Left vector.
 * @param b Right vector.
 * @return Product Vec4.
 */
Vec4 math_vec4_mulv_2(Vec4 const a, Vec4 const b);

/**
 * @brief Negate a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_negate_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the negation of a 4D vector.
 * @param v Source vector.
 * @return Negated Vec4.
 */
Vec4 math_vec4_negate_2(Vec4 const v);

/**
 * @brief Euclidean length (norm) of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @return Norm as FSize.
 */
FSize math_vec4_norm_1(FSize const *const v);

/**
 * @brief Euclidean length (norm) of a 4D vector.
 * @param v Source vector.
 * @return Norm as FSize.
 */
FSize math_vec4_norm_2(Vec4 const v);

/**
 * @brief Squared Euclidean length (norm) of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @return Squared norm as FSize.
 */
FSize math_vec4_norm2_1(FSize const *const v);

/**
 * @brief Squared Euclidean length (norm) of a 4D vector.
 * @param v Source vector.
 * @return Squared norm as FSize.
 */
FSize math_vec4_norm2_2(Vec4 const v);

/**
 * @brief Infinity norm (largest absolute component) of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @return Infinity norm as FSize.
 */
FSize math_vec4_norm_inf_1(FSize const *const v);

/**
 * @brief Infinity norm (largest absolute component) of a 4D vector.
 * @param v Source vector.
 * @return Infinity norm as FSize.
 */
FSize math_vec4_norm_inf_2(Vec4 const v);

/**
 * @brief L1 norm (sum of absolute components) of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @return One norm as FSize.
 */
FSize math_vec4_norm_one_1(FSize const *const v);

/**
 * @brief L1 norm (sum of absolute components) of a 4D vector.
 * @param v Source vector.
 * @return One norm as FSize.
 */
FSize math_vec4_norm_one_2(Vec4 const v);

/**
 * @brief Normalize a raw 4D vector (zero vector maps to zero).
 * @param v Raw source vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_normalize_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the normalized 4D vector (zero vector maps to zero).
 * @param v Source vector.
 * @return Unit Vec4.
 */
Vec4 math_vec4_normalize_2(Vec4 const v);

/**
 * @brief Fill a raw 4D destination with ones.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_one_1(FSize *const dest);

/**
 * @brief Return a 4D vector of ones.
 * @return Vec4 with every component set to 1.
 */
Vec4 math_vec4_one_2(void);

/**
 * @brief Reflect a raw incident vector about a raw normal.
 * @param v Raw incident vector (4 contiguous FSize).
 * @param n Raw normalized normal (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_reflect_1(FSize const *const v, FSize const *const n, FSize *const dest);

/**
 * @brief Return the reflection of an incident vector about a normal.
 * @param v Incident vector.
 * @param n Normalized normal.
 * @return Reflected Vec4.
 */
Vec4 math_vec4_reflect_2(Vec4 const v, Vec4 const n);

/**
 * @brief Refract a raw incident vector through a raw normal (Snell's law).
 * @param v Raw normalized incident vector (4 contiguous FSize).
 * @param n Raw normalized normal (4 contiguous FSize).
 * @param eta Ratio of indices of refraction (incident / transmitted); NaN, non-finite or
 *        not > 0 AFTER the float conversion (so an F64 past float range too) is refused
 *        (false, zeroed dest) - cglm would report a NaN vector as a success.
 * @param dest Destination of 4 contiguous FSize; zeroed on total internal reflection.
 * @return true when refraction occurs; false on total internal reflection.
 */
bool math_vec4_refract_1(FSize const *const v, FSize const *const n, FSize const eta, FSize *const dest);

/**
 * @brief Refract an incident vector through a normal (Snell's law).
 * @param v Normalized incident vector.
 * @param n Normalized normal.
 * @param eta Ratio of indices of refraction (incident / transmitted); NaN, non-finite or
 *        not > 0 after the float conversion (the F64 is bounded to float range before the
 *        cast, so a value past float range too) is refused to { false, zero }.
 * @return Vec4Refraction: refracted, and the refracted vector v when it did (zero on total
 *        internal reflection).
 */
Vec4Refraction math_vec4_refract_2(Vec4 const v, Vec4 const n, FSize const eta);

/**
 * @brief Scale a raw 4D vector by a scalar.
 * @param v Raw source vector (4 contiguous FSize).
 * @param s Scalar factor.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_scale_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 4D vector scaled by a scalar.
 * @param v Source vector.
 * @param s Scalar factor.
 * @return Scaled Vec4.
 */
Vec4 math_vec4_scale_2(Vec4 const v, FSize const s);

/**
 * @brief Scale a raw 4D vector to a given length.
 * @param v Raw source vector (4 contiguous FSize).
 * @param s Target length.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_scale_as_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 4D vector scaled to a given length.
 * @param v Source vector.
 * @param s Target length.
 * @return Scaled Vec4.
 */
Vec4 math_vec4_scale_as_2(Vec4 const v, FSize const s);

/**
 * @brief Component-wise sign of a raw 4D vector (-1, 0, or 1 per component).
 * @param v Raw source vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_sign_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise sign of a 4D vector (-1, 0, or 1 per component).
 * @param v Source vector.
 * @return Sign Vec4.
 */
Vec4 math_vec4_sign_2(Vec4 const v);

/**
 * @brief Smooth interpolation between two raw 4D vectors (smoothstep easing on t).
 * @param from Raw start vector (4 contiguous FSize).
 * @param to Raw end vector (4 contiguous FSize).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_smoothinterp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the smooth interpolation between two 4D vectors.
 * @param from Start vector.
 * @param to End vector.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated Vec4.
 */
Vec4 math_vec4_smoothinterp_2(Vec4 const from, Vec4 const to, FSize const t);

/**
 * @brief Clamped smooth interpolation between two raw 4D vectors (t clamped to [0,1]).
 * @param from Raw start vector (4 contiguous FSize).
 * @param to Raw end vector (4 contiguous FSize).
 * @param t Interpolation factor; clamped into [0, 1].
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_smoothinterpc_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the clamped smooth interpolation between two 4D vectors.
 * @param from Start vector.
 * @param to End vector.
 * @param t Interpolation factor; clamped into [0, 1].
 * @return Interpolated Vec4.
 */
Vec4 math_vec4_smoothinterpc_2(Vec4 const from, Vec4 const to, FSize const t);

/**
 * @brief Component-wise smoothstep of a raw 4D value against raw edge vectors.
 * @param edge0 Raw lower edge vector (4 contiguous FSize).
 * @param edge1 Raw upper edge vector (4 contiguous FSize).
 * @param x Raw value vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_smoothstep_1(FSize const *const edge0, FSize const *const edge1, FSize const *const x, FSize *const dest);

/**
 * @brief Return the component-wise smoothstep of a 4D value against edge vectors.
 * @param edge0 Lower edge vector.
 * @param edge1 Upper edge vector.
 * @param x Value vector.
 * @return Smoothstep result Vec4.
 */
Vec4 math_vec4_smoothstep_2(Vec4 const edge0, Vec4 const edge1, Vec4 const x);

/**
 * @brief Component-wise smoothstep of a raw 4D value against scalar edges.
 * @param edge0 Lower scalar edge.
 * @param edge1 Upper scalar edge.
 * @param x Raw value vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_smoothstep_uni_1(FSize const edge0, FSize const edge1, FSize const *const x, FSize *const dest);

/**
 * @brief Return the component-wise smoothstep of a 4D value against scalar edges.
 * @param edge0 Lower scalar edge.
 * @param edge1 Upper scalar edge.
 * @param x Value vector.
 * @return Smoothstep result Vec4.
 */
Vec4 math_vec4_smoothstep_uni_2(FSize const edge0, FSize const edge1, Vec4 const x);

/**
 * @brief Component-wise square root of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_sqrt_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise square root of a 4D vector.
 * @param v Source vector.
 * @return Square-root Vec4.
 */
Vec4 math_vec4_sqrt_2(Vec4 const v);

/**
 * @brief Component-wise step of a raw 4D vector against a raw edge vector.
 * @param edge Raw threshold vector (4 contiguous FSize).
 * @param x Raw value vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_step_1(FSize const *const edge, FSize const *const x, FSize *const dest);

/**
 * @brief Return the component-wise step of a 4D vector against an edge vector.
 * @param edge Threshold vector.
 * @param x Value vector.
 * @return Step result Vec4 (0 or 1 per component).
 */
Vec4 math_vec4_step_2(Vec4 const edge, Vec4 const x);

/**
 * @brief Step a raw 4D value vector against a scalar edge.
 * @param edge Scalar threshold.
 * @param x Raw value vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_steps_1(FSize const edge, FSize const *const x, FSize *const dest);

/**
 * @brief Return the step of a 4D value vector against a scalar edge.
 * @param edge Scalar threshold.
 * @param x Value vector.
 * @return Step result Vec4 (0 or 1 per component).
 */
Vec4 math_vec4_steps_2(FSize const edge, Vec4 const x);

/**
 * @brief Step a scalar value against a raw 4D edge vector.
 * @param edge Raw threshold vector (4 contiguous FSize).
 * @param x Scalar value to test.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_stepr_1(FSize const *const edge, FSize const x, FSize *const dest);

/**
 * @brief Return the step of a scalar value against a 4D edge vector.
 * @param edge Threshold vector.
 * @param x Scalar value to test.
 * @return Step result Vec4 (0 or 1 per component).
 */
Vec4 math_vec4_stepr_2(Vec4 const edge, FSize const x);

/**
 * @brief Subtract two raw 4D vectors (a - b).
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_sub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the difference of two 4D vectors (a - b).
 * @param a Left vector.
 * @param b Right vector.
 * @return Difference Vec4.
 */
Vec4 math_vec4_sub_2(Vec4 const a, Vec4 const b);

/**
 * @brief Accumulate (a - b) into a raw destination: dest += a - b.
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_subadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + (a - b) for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_subadd_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Subtract a scalar from every component of a raw 4D vector.
 * @param v Raw source vector (4 contiguous FSize).
 * @param s Scalar subtrahend.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_subs_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 4D vector with a scalar subtracted from every component.
 * @param v Source vector.
 * @param s Scalar subtrahend.
 * @return Result Vec4.
 */
Vec4 math_vec4_subs_2(Vec4 const v, FSize const s);

/**
 * @brief Subtract (a - b) from a raw destination: dest -= a - b.
 * @param a Raw left vector (4 contiguous FSize).
 * @param b Raw right vector (4 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous FSize).
 */
void math_vec4_subsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - (a - b) for 4D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec4.
 */
Vec4 math_vec4_subsub_2(Vec4 const a, Vec4 const b, Vec4 const accumulator);

/**
 * @brief Swizzle a raw 4D vector by a component mask.
 * @param v Raw source vector (4 contiguous FSize).
 * @param mask A MATH_SWIZZLE_* mask for THIS arity; a mask whose first 4 lanes hold an index
 *        at or past the arity (another vector size's mask) is refused and the result is
 *        zeroed. Lanes past the arity are never read, so a wider mask with in-range low
 *        lanes is accepted.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_swizzle_1(FSize const *const v, ISize const mask, FSize *const dest);

/**
 * @brief Return a 4D vector swizzled by a component mask.
 * @param v Source vector.
 * @param mask A MATH_SWIZZLE_* mask for THIS arity; a mask whose first 4 lanes hold an index
 *        at or past the arity (another vector size's mask) is refused and the result is
 *        zeroed. Lanes past the arity are never read, so a wider mask with in-range low
 *        lanes is accepted.
 * @return Swizzled Vec4.
 */
Vec4 math_vec4_swizzle_2(Vec4 const v, ISize const mask);

/**
 * @brief Copy a raw 4D vector without alignment assumptions.
 * @param a Raw source vector (4 contiguous FSize).
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_ucopy_1(FSize const *const a, FSize *const dest);

/**
 * @brief Return an unaligned copy of a 4D vector.
 * @param a Source vector.
 * @return Copied Vec4.
 */
Vec4 math_vec4_ucopy_2(Vec4 const a);

/**
 * @brief Build a raw 4D vector from a raw 3D vector plus a trailing scalar.
 * @param v Raw source vector (3 contiguous FSize).
 * @param last Scalar written to the w component.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_vec4_1(FSize const *const v, FSize const last, FSize *const dest);

/**
 * @brief Return a 4D vector built from a 3D vector plus a trailing scalar.
 * @param v Source 3D vector.
 * @param last Scalar written to the w component.
 * @return Constructed Vec4.
 */
Vec4 math_vec4_vec4_2(Vec3 const v, FSize const last);

/**
 * @brief Fill a raw 4D destination with zeros.
 * @param dest Destination of 4 contiguous FSize.
 */
void math_vec4_zero_1(FSize *const dest);

/**
 * @brief Return a 4D zero vector.
 * @return Vec4 with every component set to 0.
 */
Vec4 math_vec4_zero_2(void);

#endif // MATH_VEC4_H