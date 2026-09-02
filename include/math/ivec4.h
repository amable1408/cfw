/*
 * ivec4.h - 4D integer vector operations for the CFW math module
 *
 * Features:
 *   - Full coverage of cglm's compiled glmc_ivec4_* API: construction, copy,
 *     arithmetic, fused accumulate/subtract, geometry (distance), component-wise
 *     min/max, clamping, absolute value
 *   - Root-first variants: _1 raw ISize-array underset, _2 framework-struct superset
 *   - Pure-value semantics: no wrapper mutates its argument in place; the raw (_1)
 *     variant writes a caller-supplied destination, the struct (_2) variant returns
 *     an IVec4 (or an ISize/FSize for scalar results)
 *
 * Usage Examples:
 *   @code
 *   IVec4 const a   = { 1, 2, 3, 4 };
 *   IVec4 const b   = { 5, 6, 7, 8 };
 *   IVec4 const sum = math_ivec4_add_2(a, b);
 *   @endcode
 *
 * Error Handling:
 *   - Pointer (_1) variants validate every pointer with error_check_null.
 *   - Struct (_2) variants take values, so there is no pointer to validate.
 *   - The arithmetic runs in cglm's int (32-bit). Components are converted ISize -> int at
 *     the boundary, so a component outside [INT_MIN, INT_MAX] is TRUNCATED, and in-range
 *     components can still overflow int - undefined behaviour, not a wrap: add/sub near
 *     the limits; mul once |component| exceeds 46,340; distance2 (four summed products)
 *     subtracts first, so its bound is on the DIFFERENCE of each component pair (23,170),
 *     not on the components themselves.
 *     Callers own that domain. clamp's min/max bounds are the one exception: they are
 *     SATURATED to the int range, because a truncated bound silently inverts the clamp.
 *
 * Thread Safety:
 *   - All functions are pure and thread-safe; no shared state is touched.
 *
 * Memory Management:
 *   - No allocation is performed; every function operates on values only.
 *
 * Performance Characteristics:
 *   - Each wrapper converts ISize<->int at the boundary and calls a compiled
 *     glmc_* routine. cglm's ivec4 routines are scalar C behind a non-inlinable call -
 *     only its vec4/mat4 paths are SIMD-accelerated.
 *   - Precision: every value crosses ISize -> int -> ISize; see the Precision section
 *     in types.h for what that loses and where it flips a result.
 *
 * Dependencies:
 *   - <math/types.h> for the IVec4 type, the raw<->cglm bridges, cglm, and the
 *     error/tracing macros.
 *
 * See ivec4.c for implementation details.
 */

#ifndef MATH_IVEC4_H
#define MATH_IVEC4_H

#include <math/types.h>

/*==============================================================================
 * MARK: - IVec4 API
 *
 * Raw (_1) variants read and write 4 contiguous ISize; the struct (_2) variants
 * read and return an IVec4 value. Accumulator ops (addadd, muladd, ...) take the
 * current destination value as an input and produce the accumulated result.
 *============================================================================*/

/**
 * @brief Component-wise absolute value of a raw 4D integer vector.
 * @param v Raw source vector (4 contiguous ISize).
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_abs_1(ISize const *const v, ISize *const dest);

/**
 * @brief Return the component-wise absolute value of a 4D integer vector.
 * @param v Source vector.
 * @return Absolute-value IVec4.
 */
IVec4 math_ivec4_abs_2(IVec4 const v);

/**
 * @brief Add two raw 4D integer vectors.
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_add_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the sum of two 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Sum IVec4.
 */
IVec4 math_ivec4_add_2(IVec4 const a, IVec4 const b);

/**
 * @brief Accumulate (a + b) into a raw destination: dest += a + b.
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_addadd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + a + b for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_addadd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Accumulate (a + s) into a raw destination: dest += a + s.
 * @param a Raw source vector (4 contiguous ISize).
 * @param s Scalar addend.
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_addadds_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + (a + s) for a 4D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar addend.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_addadds_2(IVec4 const a, ISize const s, IVec4 const accumulator);

/**
 * @brief Add a scalar to every component of a raw 4D integer vector.
 * @param v Raw source vector (4 contiguous ISize).
 * @param s Scalar addend.
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_adds_1(ISize const *const v, ISize const s, ISize *const dest);

/**
 * @brief Return a 4D integer vector with a scalar added to every component.
 * @param v Source vector.
 * @param s Scalar addend.
 * @return Result IVec4.
 */
IVec4 math_ivec4_adds_2(IVec4 const v, ISize const s);

/**
 * @brief Subtract (a + b) from a raw destination: dest -= a + b.
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_addsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - (a + b) for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_addsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Subtract (a + s) from a raw destination: dest -= a + s.
 * @param a Raw source vector (4 contiguous ISize).
 * @param s Scalar addend.
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_addsubs_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - (a + s) for a 4D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar addend.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_addsubs_2(IVec4 const a, ISize const s, IVec4 const accumulator);

/**
 * @brief Clamp every component of a raw 4D integer vector into [minval, maxval].
 * @param v Raw source vector (4 contiguous ISize).
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_clamp_1(ISize const *const v, ISize const minval, ISize const maxval, ISize *const dest);

/**
 * @brief Return a 4D integer vector with every component clamped into [minval, maxval].
 * @param v Source vector.
 * @param minval Lower bound.
 * @param maxval Upper bound.
 * @return Clamped IVec4.
 */
IVec4 math_ivec4_clamp_2(IVec4 const v, ISize const minval, ISize const maxval);

/**
 * @brief Copy a raw 4D integer vector.
 * @param a Raw source vector (4 contiguous ISize).
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_copy_1(ISize const *const a, ISize *const dest);

/**
 * @brief Return a copy of a 4D integer vector.
 * @param a Source vector.
 * @return Copied IVec4.
 */
IVec4 math_ivec4_copy_2(IVec4 const a);

/**
 * @brief Euclidean distance between two raw 4D integer vectors.
 * @param a Raw first vector (4 contiguous ISize).
 * @param b Raw second vector (4 contiguous ISize).
 * @return Distance as FSize.
 */
FSize math_ivec4_distance_1(ISize const *const a, ISize const *const b);

/**
 * @brief Euclidean distance between two 4D integer vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Distance as FSize.
 */
FSize math_ivec4_distance_2(IVec4 const a, IVec4 const b);

/**
 * @brief Squared Euclidean distance between two raw 4D integer vectors.
 * @param a Raw first vector (4 contiguous ISize).
 * @param b Raw second vector (4 contiguous ISize).
 * @return Squared distance as ISize.
 */
ISize math_ivec4_distance2_1(ISize const *const a, ISize const *const b);

/**
 * @brief Squared Euclidean distance between two 4D integer vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Squared distance as ISize.
 */
ISize math_ivec4_distance2_2(IVec4 const a, IVec4 const b);

/**
 * @brief Construct a raw 4D integer vector from a raw 3D source and a last component.
 * @param v Raw source vector (3 contiguous ISize).
 * @param last Value written to the fourth component.
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_ivec4_1(ISize const *const v, ISize const last, ISize *const dest);

/**
 * @brief Construct a 4D integer vector from a 3D vector and a last component.
 * @param v Source 3D vector.
 * @param last Value written to the fourth component.
 * @return Constructed IVec4.
 */
IVec4 math_ivec4_ivec4_2(IVec3 const v, ISize const last);

/**
 * @brief Accumulate the component-wise maximum of a and b into a raw destination:
 *        dest += max(a, b).
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_maxadd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return accumulator + max(a, b) for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_maxadd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Subtract the component-wise maximum of a and b from a raw destination:
 *        dest -= max(a, b).
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_maxsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return accumulator - max(a, b) for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_maxsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Component-wise maximum of two raw 4D integer vectors.
 * @param a Raw first vector (4 contiguous ISize).
 * @param b Raw second vector (4 contiguous ISize).
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_maxv_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the component-wise maximum of two 4D integer vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise maximum IVec4.
 */
IVec4 math_ivec4_maxv_2(IVec4 const a, IVec4 const b);

/**
 * @brief Accumulate the component-wise minimum of a and b into a raw destination:
 *        dest += min(a, b).
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_minadd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return accumulator + min(a, b) for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_minadd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Subtract the component-wise minimum of a and b from a raw destination:
 *        dest -= min(a, b).
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_minsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return accumulator - min(a, b) for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_minsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Component-wise minimum of two raw 4D integer vectors.
 * @param a Raw first vector (4 contiguous ISize).
 * @param b Raw second vector (4 contiguous ISize).
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_minv_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the component-wise minimum of two 4D integer vectors.
 * @param a First vector.
 * @param b Second vector.
 * @return Component-wise minimum IVec4.
 */
IVec4 math_ivec4_minv_2(IVec4 const a, IVec4 const b);

/**
 * @brief Component-wise multiplication of two raw 4D integer vectors.
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_mul_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the component-wise multiplication of two 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @return Product IVec4.
 */
IVec4 math_ivec4_mul_2(IVec4 const a, IVec4 const b);

/**
 * @brief Accumulate (a * b) into a raw destination: dest += a * b.
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_muladd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + a * b for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_muladd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Accumulate (a * s) into a raw destination: dest += a * s.
 * @param a Raw source vector (4 contiguous ISize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_muladds_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + a * s for a 4D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_muladds_2(IVec4 const a, ISize const s, IVec4 const accumulator);

/**
 * @brief Subtract (a * b) from a raw destination: dest -= a * b.
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_mulsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - a * b for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_mulsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Subtract (a * s) from a raw destination: dest -= a * s.
 * @param a Raw source vector (4 contiguous ISize).
 * @param s Scalar factor.
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_mulsubs_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - a * s for a 4D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar factor.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_mulsubs_2(IVec4 const a, ISize const s, IVec4 const accumulator);

/**
 * @brief Fill a raw 4D integer destination with ones.
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_one_1(ISize *const dest);

/**
 * @brief Return a 4D integer vector of ones.
 * @return IVec4 with every component set to 1.
 */
IVec4 math_ivec4_one_2(void);

/**
 * @brief Scale a raw 4D integer vector by a scalar.
 * @param v Raw source vector (4 contiguous ISize).
 * @param s Scalar factor.
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_scale_1(ISize const *const v, ISize const s, ISize *const dest);

/**
 * @brief Return a 4D integer vector scaled by a scalar.
 * @param v Source vector.
 * @param s Scalar factor.
 * @return Scaled IVec4.
 */
IVec4 math_ivec4_scale_2(IVec4 const v, ISize const s);

/**
 * @brief Subtract two raw 4D integer vectors (a - b).
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_sub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the difference of two 4D integer vectors (a - b).
 * @param a Left vector.
 * @param b Right vector.
 * @return Difference IVec4.
 */
IVec4 math_ivec4_sub_2(IVec4 const a, IVec4 const b);

/**
 * @brief Accumulate (a - b) into a raw destination: dest += a - b.
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_subadd_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + (a - b) for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_subadd_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Accumulate (a - s) into a raw destination: dest += a - s.
 * @param a Raw source vector (4 contiguous ISize).
 * @param s Scalar subtrahend.
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_subadds_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator + (a - s) for a 4D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar subtrahend.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_subadds_2(IVec4 const a, ISize const s, IVec4 const accumulator);

/**
 * @brief Subtract a scalar from every component of a raw 4D integer vector.
 * @param v Raw source vector (4 contiguous ISize).
 * @param s Scalar subtrahend.
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_subs_1(ISize const *const v, ISize const s, ISize *const dest);

/**
 * @brief Return a 4D integer vector with a scalar subtracted from every component.
 * @param v Source vector.
 * @param s Scalar subtrahend.
 * @return Result IVec4.
 */
IVec4 math_ivec4_subs_2(IVec4 const v, ISize const s);

/**
 * @brief Subtract (a - b) from a raw destination: dest -= a - b.
 * @param a Raw left vector (4 contiguous ISize).
 * @param b Raw right vector (4 contiguous ISize).
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_subsub_1(ISize const *const a, ISize const *const b, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - (a - b) for 4D integer vectors.
 * @param a Left vector.
 * @param b Right vector.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_subsub_2(IVec4 const a, IVec4 const b, IVec4 const accumulator);

/**
 * @brief Subtract (a - s) from a raw destination: dest -= a - s.
 * @param a Raw source vector (4 contiguous ISize).
 * @param s Scalar subtrahend.
 * @param dest Raw accumulator, read then overwritten (4 contiguous ISize).
 */
void math_ivec4_subsubs_1(ISize const *const a, ISize const s, ISize *const dest);

/**
 * @brief Return the accumulation accumulator - (a - s) for a 4D integer vector and scalar.
 * @param a Source vector.
 * @param s Scalar subtrahend.
 * @param accumulator Accumulator base value.
 * @return Accumulated IVec4.
 */
IVec4 math_ivec4_subsubs_2(IVec4 const a, ISize const s, IVec4 const accumulator);

/**
 * @brief Fill a raw 4D integer destination with zeros.
 * @param dest Destination of 4 contiguous ISize.
 */
void math_ivec4_zero_1(ISize *const dest);

/**
 * @brief Return a 4D integer zero vector.
 * @return IVec4 with every component set to 0.
 */
IVec4 math_ivec4_zero_2(void);

#endif // MATH_IVEC4_H