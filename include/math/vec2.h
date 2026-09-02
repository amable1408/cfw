/*
 * vec2.h - 2D vector operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_vec2_* API: construction, comparison,
 *     arithmetic, fused accumulate/subtract, geometry (dot/cross/norm/distance),
 *     interpolation, stepping, clamping, complex-number ops, reflect/refract
 *   - Root-first variants: _1 raw FSize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     a Vec2 (or an FSize/bool for scalar/boolean results)
 *
 * Usage Examples:
 *   @code
 *   Vec2 const a   = { 1.0, 2.0 };
 *   Vec2 const b   = { 3.0, 4.0 };
 *   Vec2 const sum = math_vec2_add_2(a, b);
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
 *     glmc_* routine. cglm's vec2 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses F64 -> float -> F64; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the Vec2 type, the raw<->cglm bridges, cglm, and the
 *     error/tracing macros.
 *
 * See vec2.c for implementation details.
 */

#ifndef MATH_VEC2_H
#define MATH_VEC2_H

#include <math/types.h>

/*==============================================================================
 * MARK: - Constants
 *============================================================================*/

/* Swizzle masks for math_vec2_swizzle_*. Built with cglm's GLM_SHUFFLE2(y, x) so the value handed
 * to cglm is the identity; the named forms keep GLM_* out of callers. MATH_SWIZZLE2 builds any
 * other permutation from the source index of each output lane, in output order; every argument
 * must be 0..1 - GLM_SHUFFLE shifts without masking, so a larger value corrupts the next lane's
 * index instead of failing. */
#define MATH_SWIZZLE2(first, second) GLM_SHUFFLE2(second, first)
#define MATH_SWIZZLE_XX GLM_SHUFFLE2(0, 0)
#define MATH_SWIZZLE_XY GLM_SHUFFLE2(1, 0)
#define MATH_SWIZZLE_YX GLM_SHUFFLE2(0, 1)
#define MATH_SWIZZLE_YY GLM_SHUFFLE2(1, 1)

/*==============================================================================
 * MARK: - Vec2 API
 *
 * Raw (_1) variants read and write 2 contiguous FSize; the struct (_2) variants
 * read and return a Vec2 value. Accumulator ops (addadd, muladd, ...) take the
 * current destination value as an input and produce the accumulated result.
 *============================================================================*/

/**
 * @brief Component-wise absolute value of a raw 2D vector.
 * @param v Raw source vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_abs_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise absolute value of a 2D vector.
 * @param v Source vector.
 * @return Absolute-value Vec2.
 */
Vec2 math_vec2_abs_2(Vec2 const v);

/**
 * @brief Add two raw 2D vectors.
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_add_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the sum of two 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Sum Vec2.
 */
Vec2 math_vec2_add_2(Vec2 const a, Vec2 const b);

/**
 * @brief Accumulate (a + b) into a raw destination: dest += a + b.
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_addadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + a + b for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_addadd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Add a scalar to every component of a raw 2D vector.
 * @param v Raw source vector (2 contiguous FSize).
 * @param s Scalar addend.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_adds_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 2D vector with a scalar added to every component.
 * @param v Source vector.
 * @param s Scalar addend.
 * @return Result Vec2.
 */
Vec2 math_vec2_adds_2(Vec2 const v, FSize const s);

/**
 * @brief Subtract (a + b) from a raw destination: dest -= a + b.
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_addsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - (a + b) for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_addsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Midpoint of two raw 2D vectors.
 * @param a Raw first vector (2 contiguous FSize).
 * @param b Raw second vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_center_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the midpoint of two 2D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Midpoint Vec2.
 */
Vec2 math_vec2_center_2(Vec2 const a, Vec2 const b);

/**
 * @brief Clamp every component of a raw 2D vector into [minval, maxval].
 * @param v Raw source vector (2 contiguous FSize).
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_clamp_1(FSize const *const v, FSize const minval, FSize const maxval, FSize *const dest);

/**
 * @brief Return a 2D vector with every component clamped into [minval, maxval].
 * @param v Source vector.
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @return Clamped Vec2.
 */
Vec2 math_vec2_clamp_2(Vec2 const v, FSize const minval, FSize const maxval);

/**
 * @brief Complex-number conjugate of a raw 2D vector (x, -y).
 * @param a Raw source vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_complex_conjugate_1(FSize const *const a, FSize *const dest);

/**
 * @brief Return the complex-number conjugate of a 2D vector (x, -y).
 * @param a Source vector.
 * @return Conjugate Vec2.
 */
Vec2 math_vec2_complex_conjugate_2(Vec2 const a);

/**
 * @brief Complex-number division of two raw 2D vectors (a / b).
 * @param a Raw numerator (2 contiguous FSize).
 * @param b Raw denominator (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_complex_div_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the complex-number division of two 2D vectors (a / b).
 * @param a Numerator.
 * @param b Denominator.
 * @return Quotient Vec2.
 */
Vec2 math_vec2_complex_div_2(Vec2 const a, Vec2 const b);

/**
 * @brief Complex-number multiplication of two raw 2D vectors (a * b).
 * @param a Raw left number (2 contiguous FSize).
 * @param b Raw right number (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_complex_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the complex-number multiplication of two 2D vectors (a * b).
 * @param a Left number.
 * @param b Right number.
 * @return Product Vec2.
 */
Vec2 math_vec2_complex_mul_2(Vec2 const a, Vec2 const b);

/**
 * @brief Copy a raw 2D vector.
 * @param a Raw source vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_copy_1(FSize const *const a, FSize *const dest);

/**
 * @brief Return a copy of a 2D vector.
 * @param a Source vector.
 * @return Copied Vec2.
 */
Vec2 math_vec2_copy_2(Vec2 const a);

/**
 * @brief 2D cross product (z of the 3D cross) of two raw vectors.
 * @param a Raw first vector (2 contiguous FSize).
 * @param b Raw second vector (2 contiguous FSize).
 * @return Cross product as FSize.
 */
FSize math_vec2_cross_1(FSize const *const a, FSize const *const b);

/**
 * @brief 2D cross product (z of the 3D cross) of two vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Cross product as FSize.
 */
FSize math_vec2_cross_2(Vec2 const a, Vec2 const b);

/**
 * @brief Euclidean distance between two raw 2D vectors.
 * @param a Raw first vector (2 contiguous FSize).
 * @param b Raw second vector (2 contiguous FSize).
 * @return Distance as FSize.
 */
FSize math_vec2_distance_1(FSize const *const a, FSize const *const b);

/**
 * @brief Euclidean distance between two 2D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Distance as FSize.
 */
FSize math_vec2_distance_2(Vec2 const a, Vec2 const b);

/**
 * @brief Squared Euclidean distance between two raw 2D vectors.
 * @param a Raw first vector (2 contiguous FSize).
 * @param b Raw second vector (2 contiguous FSize).
 * @return Squared distance as FSize.
 */
FSize math_vec2_distance2_1(FSize const *const a, FSize const *const b);

/**
 * @brief Squared Euclidean distance between two 2D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Squared distance as FSize.
 */
FSize math_vec2_distance2_2(Vec2 const a, Vec2 const b);

/**
 * @brief Component-wise division of two raw 2D vectors (a / b).
 * @param a Raw numerator (2 contiguous FSize).
 * @param b Raw denominator (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_div_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise division of two 2D vectors (a / b).
 * @param a Numerator.
 * @param b Denominator.
 * @return Quotient Vec2.
 */
Vec2 math_vec2_div_2(Vec2 const a, Vec2 const b);

/**
 * @brief Divide every component of a raw 2D vector by a scalar.
 * @param v Raw source vector (2 contiguous FSize).
 * @param s Scalar divisor.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_divs_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 2D vector with every component divided by a scalar.
 * @param v Source vector.
 * @param s Scalar divisor.
 * @return Result Vec2.
 */
Vec2 math_vec2_divs_2(Vec2 const v, FSize const s);

/**
 * @brief Dot product of two raw 2D vectors.
 * @param a Raw first vector (2 contiguous FSize).
 * @param b Raw second vector (2 contiguous FSize).
 * @return Dot product as FSize.
 */
FSize math_vec2_dot_1(FSize const *const a, FSize const *const b);

/**
 * @brief Dot product of two 2D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Dot product as FSize.
 */
FSize math_vec2_dot_2(Vec2 const a, Vec2 const b);

/**
 * @brief Test whether every component of a raw 2D vector equals a scalar.
 * @param v Raw source vector (2 contiguous FSize).
 * @param val Scalar to compare against.
 * @return true when both components equal val.
 */
bool math_vec2_eq_1(FSize const *const v, FSize const val);

/**
 * @brief Test whether every component of a 2D vector equals a scalar.
 * @param v Source vector.
 * @param val Scalar to compare against.
 * @return true when both components equal val.
 */
bool math_vec2_eq_2(Vec2 const v, FSize const val);

/**
 * @brief Test two raw 2D vectors for exact equality.
 * @param a Raw first vector (2 contiguous FSize).
 * @param b Raw second vector (2 contiguous FSize).
 * @return true when all components are equal.
 */
bool math_vec2_eqv_1(FSize const *const a, FSize const *const b);

/**
 * @brief Test two 2D vectors for exact equality.
 * @param a First vector.
 * @param b Second vector.
 * @return true when all components are equal.
 */
bool math_vec2_eqv_2(Vec2 const a, Vec2 const b);

/**
 * @brief Fill a raw 2D destination with a single scalar value.
 * @param val Scalar written to every component.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_fill_1(FSize const val, FSize *const dest);

/**
 * @brief Return a 2D vector with every component set to a scalar value.
 * @param val Scalar written to every component.
 * @return Filled Vec2.
 */
Vec2 math_vec2_fill_2(FSize const val);

/**
 * @brief Component-wise floor of a raw 2D vector.
 * @param v Raw source vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_floor_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise floor of a 2D vector.
 * @param v Source vector.
 * @return Floored Vec2.
 */
Vec2 math_vec2_floor_2(Vec2 const v);

/**
 * @brief Component-wise fractional part of a raw 2D vector.
 * @param v Raw source vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_fract_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the component-wise fractional part of a 2D vector.
 * @param v Source vector.
 * @return Fractional Vec2.
 */
Vec2 math_vec2_fract_2(Vec2 const v);

/**
 * @brief Linearly interpolate between two raw 2D vectors.
 * @param from Raw start vector (2 contiguous FSize).
 * @param to Raw end vector (2 contiguous FSize).
 * @param t Interpolation factor in [0, 1].
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_lerp_1(FSize const *const from, FSize const *const to, FSize const t, FSize *const dest);

/**
 * @brief Return the linear interpolation between two 2D vectors.
 * @param from Start vector.
 * @param to End vector.
 * @param t Interpolation factor in [0, 1].
 * @return Interpolated Vec2.
 */
Vec2 math_vec2_lerp_2(Vec2 const from, Vec2 const to, FSize const t);

/**
 * @brief Construct a raw 2D vector from a raw FSize source array.
 * @param src Raw source array (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_make_1(FSize const *const src, FSize *const dest);

/**
 * @brief Construct a 2D vector from a raw FSize source array.
 * @param src Raw source array (2 contiguous FSize).
 * @return Constructed Vec2.
 */
Vec2 math_vec2_make_2(FSize const *const src);

/**
 * @brief Accumulate the component-wise maximum of a and b into a raw destination:
 *        dest += max(a, b).
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_maxadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator + max(a, b) for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_maxadd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Subtract the component-wise maximum of a and b from a raw destination:
 *        dest -= max(a, b).
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_maxsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator - max(a, b) for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_maxsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Component-wise maximum of two raw 2D vectors.
 * @param a Raw first vector (2 contiguous FSize).
 * @param b Raw second vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_maxv_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise maximum of two 2D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise maximum Vec2.
 */
Vec2 math_vec2_maxv_2(Vec2 const a, Vec2 const b);

/**
 * @brief Accumulate the component-wise minimum of a and b into a raw destination:
 *        dest += min(a, b).
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_minadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator + min(a, b) for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_minadd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Subtract the component-wise minimum of a and b from a raw destination:
 *        dest -= min(a, b).
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_minsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return accumulator - min(a, b) for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_minsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Component-wise minimum of two raw 2D vectors.
 * @param a Raw first vector (2 contiguous FSize).
 * @param b Raw second vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_minv_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise minimum of two 2D vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise minimum Vec2.
 */
Vec2 math_vec2_minv_2(Vec2 const a, Vec2 const b);

/**
 * @brief Component-wise modulo of a raw 2D vector by a scalar.
 * @param v Raw source vector (2 contiguous FSize).
 * @param s Scalar divisor.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_mods_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return the component-wise modulo of a 2D vector by a scalar.
 * @param v Source vector.
 * @param s Scalar divisor.
 * @return Result Vec2.
 */
Vec2 math_vec2_mods_2(Vec2 const v, FSize const s);

/**
 * @brief Component-wise multiplication of two raw 2D vectors.
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_mul_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the component-wise multiplication of two 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Product Vec2.
 */
Vec2 math_vec2_mul_2(Vec2 const a, Vec2 const b);

/**
 * @brief Accumulate (a * b) into a raw destination: dest += a * b.
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_muladd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + a * b for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_muladd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Accumulate (a * s) into a raw destination: dest += a * s.
 * @param a Raw source vector (2 contiguous FSize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_muladds_1(FSize const *const a, FSize const s, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + a * s for a 2D vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_muladds_2(Vec2 const a, FSize const s, Vec2 const accumulator);

/**
 * @brief Subtract (a * b) from a raw destination: dest -= a * b.
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_mulsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - a * b for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_mulsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Subtract (a * s) from a raw destination: dest -= a * s.
 * @param a Raw source vector (2 contiguous FSize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_mulsubs_1(FSize const *const a, FSize const s, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - a * s for a 2D vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_mulsubs_2(Vec2 const a, FSize const s, Vec2 const accumulator);

/**
 * @brief Negate a raw 2D vector.
 * @param v Raw source vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_negate_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the negation of a 2D vector.
 * @param v Source vector.
 * @return Negated Vec2.
 */
Vec2 math_vec2_negate_2(Vec2 const v);

/**
 * @brief Euclidean length (norm) of a raw 2D vector.
 * @param v Raw source vector (2 contiguous FSize).
 * @return Norm as FSize.
 */
FSize math_vec2_norm_1(FSize const *const v);

/**
 * @brief Euclidean length (norm) of a 2D vector.
 * @param v Source vector.
 * @return Norm as FSize.
 */
FSize math_vec2_norm_2(Vec2 const v);

/**
 * @brief Squared Euclidean length (norm) of a raw 2D vector.
 * @param v Raw source vector (2 contiguous FSize).
 * @return Squared norm as FSize.
 */
FSize math_vec2_norm2_1(FSize const *const v);

/**
 * @brief Squared Euclidean length (norm) of a 2D vector.
 * @param v Source vector.
 * @return Squared norm as FSize.
 */
FSize math_vec2_norm2_2(Vec2 const v);

/**
 * @brief Normalize a raw 2D vector (zero vector maps to zero).
 * @param v Raw source vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_normalize_1(FSize const *const v, FSize *const dest);

/**
 * @brief Return the normalized 2D vector (zero vector maps to zero).
 * @param v Source vector.
 * @return Unit Vec2.
 */
Vec2 math_vec2_normalize_2(Vec2 const v);

/**
 * @brief Fill a raw 2D destination with ones.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_one_1(FSize *const dest);

/**
 * @brief Return a 2D vector of ones.
 * @return Vec2 with every component set to 1.
 */
Vec2 math_vec2_one_2(void);

/**
 * @brief Reflect a raw incident vector about a raw normal.
 * @param v Raw incident vector (2 contiguous FSize).
 * @param n Raw normalized normal (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_reflect_1(FSize const *const v, FSize const *const n, FSize *const dest);

/**
 * @brief Return the reflection of an incident vector about a normal.
 * @param v Incident vector.
 * @param n Normalized normal.
 * @return Reflected Vec2.
 */
Vec2 math_vec2_reflect_2(Vec2 const v, Vec2 const n);

/**
 * @brief Refract a raw incident vector through a raw normal (Snell's law).
 * @param v Raw normalized incident vector (2 contiguous FSize).
 * @param n Raw normalized normal (2 contiguous FSize).
 * @param eta Ratio of indices of refraction (incident / transmitted); NaN, non-finite or
 *        not > 0 AFTER the float conversion (so an F64 past float range too) is refused
 *        (false, zeroed dest) - cglm would report a NaN vector as a success.
 * @param dest Destination of 2 contiguous FSize; zeroed on total internal reflection.
 * @return true when refraction occurs; false on total internal reflection.
 */
bool math_vec2_refract_1(FSize const *const v, FSize const *const n, FSize const eta, FSize *const dest);

/**
 * @brief Refract an incident vector through a normal (Snell's law).
 * @param v Normalized incident vector.
 * @param n Normalized normal.
 * @param eta Ratio of indices of refraction (incident / transmitted); NaN, non-finite or
 *        not > 0 after the float conversion (the F64 is bounded to float range before the
 *        cast, so a value past float range too) is refused to { false, zero }.
 * @return Vec2Refraction: refracted, and the refracted vector v when it did (zero on total
 *        internal reflection).
 */
Vec2Refraction math_vec2_refract_2(Vec2 const v, Vec2 const n, FSize const eta);

/**
 * @brief Rotate a raw 2D vector by an angle in radians.
 * @param v Raw source vector (2 contiguous FSize).
 * @param angle Rotation angle in radians.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_rotate_1(FSize const *const v, FSize const angle, FSize *const dest);

/**
 * @brief Return a 2D vector rotated by an angle in radians.
 * @param v Source vector.
 * @param angle Rotation angle in radians.
 * @return Rotated Vec2.
 */
Vec2 math_vec2_rotate_2(Vec2 const v, FSize const angle);

/**
 * @brief Scale a raw 2D vector by a scalar.
 * @param v Raw source vector (2 contiguous FSize).
 * @param s Scalar factor.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_scale_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 2D vector scaled by a scalar.
 * @param v Source vector.
 * @param s Scalar factor.
 * @return Scaled Vec2.
 */
Vec2 math_vec2_scale_2(Vec2 const v, FSize const s);

/**
 * @brief Scale a raw 2D vector to a given length.
 * @param v Raw source vector (2 contiguous FSize).
 * @param s Target length.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_scale_as_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 2D vector scaled to a given length.
 * @param v Source vector.
 * @param s Target length.
 * @return Scaled Vec2.
 */
Vec2 math_vec2_scale_as_2(Vec2 const v, FSize const s);

/**
 * @brief Component-wise step of a raw 2D vector against a raw edge vector.
 * @param edge Raw threshold vector (2 contiguous FSize).
 * @param x Raw value vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_step_1(FSize const *const edge, FSize const *const x, FSize *const dest);

/**
 * @brief Return the component-wise step of a 2D vector against an edge vector.
 * @param edge Threshold vector.
 * @param x Value vector.
 * @return Step result Vec2 (0 or 1 per component).
 */
Vec2 math_vec2_step_2(Vec2 const edge, Vec2 const x);

/**
 * @brief Step a raw 2D value vector against a scalar edge.
 * @param edge Scalar threshold.
 * @param x Raw value vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_steps_1(FSize const edge, FSize const *const x, FSize *const dest);

/**
 * @brief Return the step of a 2D value vector against a scalar edge.
 * @param edge Scalar threshold.
 * @param x Value vector.
 * @return Step result Vec2 (0 or 1 per component).
 */
Vec2 math_vec2_steps_2(FSize const edge, Vec2 const x);

/**
 * @brief Step a scalar value against a raw 2D edge vector.
 * @param edge Raw threshold vector (2 contiguous FSize).
 * @param x Scalar value to test.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_stepr_1(FSize const *const edge, FSize const x, FSize *const dest);

/**
 * @brief Return the step of a scalar value against a 2D edge vector.
 * @param edge Threshold vector.
 * @param x Scalar value to test.
 * @return Step result Vec2 (0 or 1 per component).
 */
Vec2 math_vec2_stepr_2(Vec2 const edge, FSize const x);

/**
 * @brief Subtract two raw 2D vectors (a - b).
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_sub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the difference of two 2D vectors (a - b).
 * @param a Left vector.
 * @param b Right vector.
 * @return Difference Vec2.
 */
Vec2 math_vec2_sub_2(Vec2 const a, Vec2 const b);

/**
 * @brief Accumulate (a - b) into a raw destination: dest += a - b.
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_subadd_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator + (a - b) for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_subadd_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Subtract a scalar from every component of a raw 2D vector.
 * @param v Raw source vector (2 contiguous FSize).
 * @param s Scalar subtrahend.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_subs_1(FSize const *const v, FSize const s, FSize *const dest);

/**
 * @brief Return a 2D vector with a scalar subtracted from every component.
 * @param v Source vector.
 * @param s Scalar subtrahend.
 * @return Result Vec2.
 */
Vec2 math_vec2_subs_2(Vec2 const v, FSize const s);

/**
 * @brief Subtract (a - b) from a raw destination: dest -= a - b.
 * @param a Raw left vector (2 contiguous FSize).
 * @param b Raw right vector (2 contiguous FSize).
 * @param dest Raw accumulator, read then overwritten (2 contiguous FSize).
 */
void math_vec2_subsub_1(FSize const *const a, FSize const *const b, FSize *const dest);

/**
 * @brief Return the accumulation accumulator - (a - b) for 2D vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated Vec2.
 */
Vec2 math_vec2_subsub_2(Vec2 const a, Vec2 const b, Vec2 const accumulator);

/**
 * @brief Swizzle a raw 2D vector by a component mask.
 * @param v Raw source vector (2 contiguous FSize).
 * @param mask A MATH_SWIZZLE_* mask for THIS arity; a mask whose first 2 lanes hold an index
 *        at or past the arity (another vector size's mask) is refused and the result is
 *        zeroed. Lanes past the arity are never read, so a wider mask with in-range low
 *        lanes is accepted.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_swizzle_1(FSize const *const v, ISize const mask, FSize *const dest);

/**
 * @brief Return a 2D vector swizzled by a component mask.
 * @param v Source vector.
 * @param mask A MATH_SWIZZLE_* mask for THIS arity; a mask whose first 2 lanes hold an index
 *        at or past the arity (another vector size's mask) is refused and the result is
 *        zeroed. Lanes past the arity are never read, so a wider mask with in-range low
 *        lanes is accepted.
 * @return Swizzled Vec2.
 */
Vec2 math_vec2_swizzle_2(Vec2 const v, ISize const mask);

/**
 * @brief Construct a raw 2D vector by copying a raw FSize source array.
 * @param v Raw source array; only its first 2 FSize are read (the struct form takes a
 *        Vec3 and drops z - this raw form never reads a third element).
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_vec2_1(FSize const *const v, FSize *const dest);

/**
 * @brief Construct a 2D vector from the x and y of a 3D one (z is dropped).
 * @param v Source vector.
 * @return Constructed Vec2.
 */
Vec2 math_vec2_vec2_2(Vec3 const v);

/**
 * @brief Fill a raw 2D destination with zeros.
 * @param dest Destination of 2 contiguous FSize.
 */
void math_vec2_zero_1(FSize *const dest);

/**
 * @brief Return a 2D zero vector.
 * @return Vec2 with every component set to 0.
 */
Vec2 math_vec2_zero_2(void);

#endif // MATH_VEC2_H