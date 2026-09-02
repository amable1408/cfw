/*
 * ivec3.h - 3D integer vector operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_ivec3_* API: construction,
 *     comparison, arithmetic, fused accumulate/subtract, geometry
 *     (dot/norm/distance), min/max, clamp, modulo, absolute value
 *   - Root-first variants: _1 raw ISize-array underset, _2 framework-struct
 *     superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw
 *     (_1) variant writes a caller-supplied destination, the struct (_2)
 *     variant returns an IVec3 (or an ISize/FSize/bool for scalar/boolean
 *     results)
 *
 * Usage Examples:
 *   @code
 *   IVec3 const a   = { 1, 2, 3 };
 *   IVec3 const b   = { 4, 5, 6 };
 *   IVec3 const sum = math_ivec3_add_2(a, b);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - The arithmetic runs in cglm's int (32-bit). Components are converted ISize -> int at
 *     the boundary, so a component outside [INT_MIN, INT_MAX] is TRUNCATED, and in-range
 *     components can still overflow int - undefined behaviour, not a wrap: add/sub near
 *     the limits; mul once |component| exceeds 46,340; dot and norm2 (three summed
 *     products) once |component| exceeds 26,754; distance2 subtracts first, so its bound
 *     is on the DIFFERENCE of each component pair (26,754), not on the components.
 *     Callers own that domain. clamp's min/max bounds are the one exception: they are
 *     SATURATED to the int range, because a truncated bound silently inverts the clamp.
 *   - div / divs / mod REFUSE every divisor that would TRAP - zero, and -1 against an INT_MIN
 *     dividend, whose quotient does not fit - to the zeroed vector, in every build. Integer
 *     division by such a pair kills the process, and a divisor is data. The check runs on the
 *     CONVERTED ints, so a divisor whose low 32 bits are zero refuses too.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper converts ISize<->int at the boundary and calls a compiled
 *     glmc_* routine. cglm's ivec3 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses ISize -> int -> ISize; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the IVec3 type, the raw<->cglm bridges, cglm, and the
 *     error/tracing macros.
 *
 * See ivec3.c for implementation details.
 */

#ifndef MATH_IVEC3_H
#define MATH_IVEC3_H

#include <math/types.h>

/*==============================================================================
 * MARK: - IVec3 API
 *
 * Raw (_1) variants read and write 3 contiguous ISize; the struct (_2) variants
 * read and return an IVec3 value. Accumulator ops (addadd, muladd, ...) take the
 * current destination value as an input and produce the accumulated result.
 *============================================================================*/

/**
 * @brief Component-wise absolute value of a raw 3D integer vector.
 * @param v Raw source vector (3 contiguous ISize).
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_abs_1(ISize const *const v, ISize *const dest);

/**
 * @brief Return the component-wise absolute value of a 3D integer vector.
 * @param v Source vector.
 * @return Absolute-value IVec3.
 */
IVec3 math_ivec3_abs_2(IVec3 const v);

/**
 * @brief Add two raw 3D integer vectors.
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_add_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the sum of two 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Sum IVec3.
 */
IVec3 math_ivec3_add_2(IVec3 const a, IVec3 const b);

/**
 * @brief Accumulate (a + b) into a raw destination: dest += a + b.
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_addadd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + a + b for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_addadd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Accumulate (a + s) into a raw destination: dest += a + s.
 * @param a Raw source vector (3 contiguous ISize).
 * @param s Scalar addend.
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_addadds_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + a + s for a 3D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar addend.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_addadds_2(IVec3 const a, ISize const s, IVec3 const accumulator);

/**
 * @brief Add a scalar to every component of a raw 3D integer vector.
 * @param v Raw source vector (3 contiguous ISize).
 * @param s Scalar addend.
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_adds_1(ISize const *const v, ISize const s, ISize *const dest);

/**
 * @brief Return a 3D integer vector with a scalar added to every component.
 * @param v Source vector.
 * @param s Scalar addend.
 * @return Result IVec3.
 */
IVec3 math_ivec3_adds_2(IVec3 const v, ISize const s);

/**
 * @brief Subtract (a + b) from a raw destination: dest -= a + b.
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_addsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - (a + b) for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_addsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Subtract (a + s) from a raw destination: dest -= a + s.
 * @param a Raw source vector (3 contiguous ISize).
 * @param s Scalar addend.
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_addsubs_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - (a + s) for a 3D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar addend.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_addsubs_2(IVec3 const a, ISize const s, IVec3 const accumulator);

/**
 * @brief Clamp every component of a raw 3D integer vector into [minval, maxval].
 * @param v Raw source vector (3 contiguous ISize).
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_clamp_1(ISize const *const v, ISize const minval, ISize const maxval, ISize *const dest);

/**
 * @brief Return a 3D integer vector with every component clamped into [minval, maxval].
 * @param v Source vector.
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @return Clamped IVec3.
 */
IVec3 math_ivec3_clamp_2(IVec3 const v, ISize const minval, ISize const maxval);

/**
 * @brief Copy a raw 3D integer vector.
 * @param a Raw source vector (3 contiguous ISize).
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_copy_1(ISize const *const a, ISize *const dest);

/**
 * @brief Return a copy of a 3D integer vector.
 * @param a Source vector.
 * @return Copied IVec3.
 */
IVec3 math_ivec3_copy_2(IVec3 const a);

/**
 * @brief Euclidean distance between two raw 3D integer vectors.
 * @param a Raw first vector (3 contiguous ISize).
 * @param b Raw second vector (3 contiguous ISize).
 * @return Distance as FSize.
 */
FSize math_ivec3_distance_1(ISize const *const a, ISize const *const b);

/**
 * @brief Euclidean distance between two 3D integer vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Distance as FSize.
 */
FSize math_ivec3_distance_2(IVec3 const a, IVec3 const b);

/**
 * @brief Squared Euclidean distance between two raw 3D integer vectors.
 * @param a Raw first vector (3 contiguous ISize).
 * @param b Raw second vector (3 contiguous ISize).
 * @return Squared distance as ISize.
 */
ISize math_ivec3_distance2_1(ISize const *const a, ISize const *const b);

/**
 * @brief Squared Euclidean distance between two 3D integer vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Squared distance as ISize.
 */
ISize math_ivec3_distance2_2(IVec3 const a, IVec3 const b);

/**
 * @brief Component-wise integer division of two raw 3D integer vectors (a / b).
 * @param a Raw numerator (3 contiguous ISize).
 * @param b Raw denominator (3 contiguous ISize).
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_div_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the component-wise integer division of two 3D integer vectors (a / b).
 * @param a Numerator.
 * @param b Denominator.
 * @return Quotient IVec3.
 */
IVec3 math_ivec3_div_2(IVec3 const a, IVec3 const b);

/**
 * @brief Divide every component of a raw 3D integer vector by a scalar.
 * @param v Raw source vector (3 contiguous ISize).
 * @param s Scalar divisor.
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_divs_1(ISize const *const v, ISize const s, ISize *const dest);

/**
 * @brief Return a 3D integer vector with every component divided by a scalar.
 * @param v Source vector.
 * @param s Scalar divisor.
 * @return Result IVec3.
 */
IVec3 math_ivec3_divs_2(IVec3 const v, ISize const s);

/**
 * @brief Dot product of two raw 3D integer vectors.
 * @param a Raw first vector (3 contiguous ISize).
 * @param b Raw second vector (3 contiguous ISize).
 * @return Dot product as ISize.
 */
ISize math_ivec3_dot_1(ISize const *const a, ISize const *const b);

/**
 * @brief Dot product of two 3D integer vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Dot product as ISize.
 */
ISize math_ivec3_dot_2(IVec3 const a, IVec3 const b);

/**
 * @brief Test whether every component of a raw 3D integer vector equals a scalar.
 * @param v Raw source vector (3 contiguous ISize).
 * @param val Scalar to compare against.
 * @return true when all components equal val.
 */
bool math_ivec3_eq_1(ISize const *const v, ISize const val);

/**
 * @brief Test whether every component of a 3D integer vector equals a scalar.
 * @param v Source vector.
 * @param val Scalar to compare against.
 * @return true when all components equal val.
 */
bool math_ivec3_eq_2(IVec3 const v, ISize const val);

/**
 * @brief Test two raw 3D integer vectors for exact equality.
 * @param a Raw first vector (3 contiguous ISize).
 * @param b Raw second vector (3 contiguous ISize).
 * @return true when all components are equal.
 */
bool math_ivec3_eqv_1(ISize const *const a, ISize const *const b);

/**
 * @brief Test two 3D integer vectors for exact equality.
 * @param a First vector.
 * @param b Second vector.
 * @return true when all components are equal.
 */
bool math_ivec3_eqv_2(IVec3 const a, IVec3 const b);

/**
 * @brief Fill a raw 3D integer destination with a single scalar value.
 * @param val Scalar written to every component.
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_fill_1(ISize const val, ISize *const dest);

/**
 * @brief Return a 3D integer vector with every component set to a scalar value.
 * @param val Scalar written to every component.
 * @return Filled IVec3.
 */
IVec3 math_ivec3_fill_2(ISize const val);

/**
 * @brief Construct a raw 3D integer vector from a raw 4-component ISize source.
 * @param v Raw source array (4 contiguous ISize); the first 3 are copied.
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_ivec3_1(ISize const *const v, ISize *const dest);

/**
 * @brief Construct a 3D integer vector from a 4D integer vector (drops w).
 * @param v Source 4D integer vector.
 * @return Constructed IVec3.
 */
IVec3 math_ivec3_ivec3_2(IVec4 const v);

/**
 * @brief Accumulate the component-wise maximum of a and b into a raw destination:
 *        dest += max(a, b).
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_maxadd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return accumulator + max(a, b) for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_maxadd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Subtract the component-wise maximum of a and b from a raw destination:
 *        dest -= max(a, b).
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_maxsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return accumulator - max(a, b) for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_maxsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Component-wise maximum of two raw 3D integer vectors.
 * @param a Raw first vector (3 contiguous ISize).
 * @param b Raw second vector (3 contiguous ISize).
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_maxv_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the component-wise maximum of two 3D integer vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise maximum IVec3.
 */
IVec3 math_ivec3_maxv_2(IVec3 const a, IVec3 const b);

/**
 * @brief Accumulate the component-wise minimum of a and b into a raw destination:
 *        dest += min(a, b).
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_minadd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return accumulator + min(a, b) for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_minadd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Subtract the component-wise minimum of a and b from a raw destination:
 *        dest -= min(a, b).
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_minsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return accumulator - min(a, b) for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_minsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Component-wise minimum of two raw 3D integer vectors.
 * @param a Raw first vector (3 contiguous ISize).
 * @param b Raw second vector (3 contiguous ISize).
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_minv_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the component-wise minimum of two 3D integer vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise minimum IVec3.
 */
IVec3 math_ivec3_minv_2(IVec3 const a, IVec3 const b);

/**
 * @brief Component-wise integer modulo of two raw 3D integer vectors (a % b).
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_mod_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the component-wise integer modulo of two 3D integer vectors (a % b).
 * @param a Left vector.
 * @param b Right vector.
 * @return Result IVec3.
 */
IVec3 math_ivec3_mod_2(IVec3 const a, IVec3 const b);

/**
 * @brief Component-wise multiplication of two raw 3D integer vectors.
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_mul_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the component-wise multiplication of two 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Product IVec3.
 */
IVec3 math_ivec3_mul_2(IVec3 const a, IVec3 const b);

/**
 * @brief Accumulate (a * b) into a raw destination: dest += a * b.
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_muladd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + a * b for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_muladd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Accumulate (a * s) into a raw destination: dest += a * s.
 * @param a Raw source vector (3 contiguous ISize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_muladds_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + a * s for a 3D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_muladds_2(IVec3 const a, ISize const s, IVec3 const accumulator);

/**
 * @brief Subtract (a * b) from a raw destination: dest -= a * b.
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_mulsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - a * b for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_mulsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Subtract (a * s) from a raw destination: dest -= a * s.
 * @param a Raw source vector (3 contiguous ISize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_mulsubs_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - a * s for a 3D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_mulsubs_2(IVec3 const a, ISize const s, IVec3 const accumulator);

/**
 * @brief Euclidean length (norm) of a raw 3D integer vector (truncated to integer).
 * @param v Raw source vector (3 contiguous ISize).
 * @return Norm as ISize.
 */
ISize math_ivec3_norm_1(ISize const *const v);

/**
 * @brief Euclidean length (norm) of a 3D integer vector (truncated to integer).
 * @param v Source vector.
 * @return Norm as ISize.
 */
ISize math_ivec3_norm_2(IVec3 const v);

/**
 * @brief Squared Euclidean length (norm) of a raw 3D integer vector.
 * @param v Raw source vector (3 contiguous ISize).
 * @return Squared norm as ISize.
 */
ISize math_ivec3_norm2_1(ISize const *const v);

/**
 * @brief Squared Euclidean length (norm) of a 3D integer vector.
 * @param v Source vector.
 * @return Squared norm as ISize.
 */
ISize math_ivec3_norm2_2(IVec3 const v);

/**
 * @brief Fill a raw 3D integer destination with ones.
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_one_1(ISize *const dest);

/**
 * @brief Return a 3D integer vector of ones.
 * @return IVec3 with every component set to 1.
 */
IVec3 math_ivec3_one_2(void);

/**
 * @brief Scale a raw 3D integer vector by a scalar.
 * @param v Raw source vector (3 contiguous ISize).
 * @param s Scalar factor.
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_scale_1(ISize const *const v, ISize const s, ISize *const dest);

/**
 * @brief Return a 3D integer vector scaled by a scalar.
 * @param v Source vector.
 * @param s Scalar factor.
 * @return Scaled IVec3.
 */
IVec3 math_ivec3_scale_2(IVec3 const v, ISize const s);

/**
 * @brief Subtract two raw 3D integer vectors (a - b).
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_sub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the difference of two 3D integer vectors (a - b).
 * @param a Left vector.
 * @param b Right vector.
 * @return Difference IVec3.
 */
IVec3 math_ivec3_sub_2(IVec3 const a, IVec3 const b);

/**
 * @brief Accumulate (a - b) into a raw destination: dest += a - b.
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_subadd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + (a - b) for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_subadd_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Accumulate (a - s) into a raw destination: dest += a - s.
 * @param a Raw source vector (3 contiguous ISize).
 * @param s Scalar subtrahend.
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_subadds_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + (a - s) for a 3D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar subtrahend.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_subadds_2(IVec3 const a, ISize const s, IVec3 const accumulator);

/**
 * @brief Subtract a scalar from every component of a raw 3D integer vector.
 * @param v Raw source vector (3 contiguous ISize).
 * @param s Scalar subtrahend.
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_subs_1(ISize const *const v, ISize const s, ISize *const dest);

/**
 * @brief Return a 3D integer vector with a scalar subtracted from every component.
 * @param v Source vector.
 * @param s Scalar subtrahend.
 * @return Result IVec3.
 */
IVec3 math_ivec3_subs_2(IVec3 const v, ISize const s);

/**
 * @brief Subtract (a - b) from a raw destination: dest -= a - b.
 * @param a Raw left vector (3 contiguous ISize).
 * @param b Raw right vector (3 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_subsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - (a - b) for 3D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_subsub_2(IVec3 const a, IVec3 const b, IVec3 const accumulator);

/**
 * @brief Subtract (a - s) from a raw destination: dest -= a - s.
 * @param a Raw source vector (3 contiguous ISize).
 * @param s Scalar subtrahend.
 * @param dest Raw accumulator, read then overwritten (3 contiguous ISize).
 */
void math_ivec3_subsubs_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - (a - s) for a 3D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar subtrahend.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec3.
 */
IVec3 math_ivec3_subsubs_2(IVec3 const a, ISize const s, IVec3 const accumulator);

/**
 * @brief Fill a raw 3D integer destination with zeros.
 * @param dest Destination of 3 contiguous ISize.
 */
void math_ivec3_zero_1(ISize *const dest);

/**
 * @brief Return a 3D integer zero vector.
 * @return IVec3 with every component set to 0.
 */
IVec3 math_ivec3_zero_2(void);

#endif // MATH_IVEC3_H