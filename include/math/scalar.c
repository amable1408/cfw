/*
 * scalar.c - Scalar libm wrappers for the CFW math module.
 *
 * See scalar.h for API documentation and usage examples.
 */

#include <math/scalar.h>

/*==============================================================================
 * MARK: - Scalar API
 *============================================================================*/

FSize math_abs_f(FSize const value) {
    return (FSize) fabs(value);
}

ISize math_abs_i(ISize const value) {
    trace_log_push(LOG_METADATA);

    if (value < -ISIZE_MAX) {
        trace_log_pop();

        return ISIZE_MAX;
    }

    ISize const result = (ISize) llabs(value);

    trace_log_pop();

    return result;
}

FSize math_acos_f(FSize const value) {
    return (FSize) acos(value);
}

FSize math_asin_f(FSize const value) {
    return (FSize) asin(value);
}

FSize math_atan2_f(FSize const y, FSize const x) {
    return (FSize) atan2(y, x);
}

FSize math_atan_f(FSize const value) {
    return (FSize) atan(value);
}

FSize math_ceil_f(FSize const value) {
    return (FSize) ceil(value);
}

FSize math_clamp_f(FSize const value, FSize const min, FSize const max) {
    return value < min ? min : (value > max ? max : value);
}

FSize math_cos_f(FSize const value) {
    return (FSize) cos(value);
}

FSize math_exp_f(FSize const value) {
    return (FSize) exp(value);
}

FSize math_floor_f(FSize const value) {
    return (FSize) floorl(value);
}

USize math_floor_u(FSize const value) {
    trace_log_push(LOG_METADATA);

    if (value < 0 || value != value) {
        trace_log_pop();

        return 0;
    }

    FSize const floored = floorl(value);

    if (floored >= (FSize) USIZE_MAX) {
        trace_log_pop();

        return 0;
    }

    USize const result = (USize) floored;

    trace_log_pop();

    return result;
}

FSize math_fmod_f(FSize const x, FSize const y) {
    return (FSize) fmod(x, y);
}

FSize math_lerp_f(FSize const a, FSize const b, FSize const t) {
    return a + (b - a) * t;
}

FSize math_log10_f(FSize const value) {
    return (FSize) log10(value);
}

FSize math_log_f(FSize const value) {
    return (FSize) log(value);
}

FSize math_max_f(FSize const a, FSize const b) {
    return a > b ? a : b;
}

ISize math_max_i(ISize const a, ISize const b) {
    return a > b ? a : b;
}

USize math_max_u(USize const a, USize const b) {
    return a > b ? a : b;
}

FSize math_min_f(FSize const a, FSize const b) {
    return a < b ? a : b;
}

ISize math_min_i(ISize const a, ISize const b) {
    return a < b ? a : b;
}

USize math_min_u(USize const a, USize const b) {
    return a < b ? a : b;
}

FSize math_negate_f(FSize const x) {
    return x * -1;
}

USize math_negate_u(USize const x) {
    return ~x + 1;
}

FSize math_pow_f(FSize const base, FSize const exp) {
    return (FSize) powl(base, exp);
}

USize math_pow_u(FSize const base, FSize const exp) {
    trace_log_push(LOG_METADATA);

    FSize const result = powl(base, exp);

    if (result < 0 || result != result || result >= (FSize) USIZE_MAX) {
        trace_log_pop();

        return 0;
    }

    USize const converted = (USize) result;

    trace_log_pop();

    return converted;
}

FSize math_remap_f(FSize const value, FSize const in_min, FSize const in_max, FSize const out_min, FSize const out_max) {
    return out_min + (value - in_min) / (in_max - in_min) * (out_max - out_min);
}

FSize math_round_f(FSize const value) {
    return (FSize) round(value);
}

USize math_round_u(FSize const value) {
    trace_log_push(LOG_METADATA);

    if (value < 0 || value != value) {
        trace_log_pop();

        return 0;
    }

    FSize const rounded = round(value);

    if (rounded >= (FSize) USIZE_MAX) {
        trace_log_pop();

        return 0;
    }

    USize const result = (USize) rounded;

    trace_log_pop();

    return result;
}

FSize math_sin_f(FSize const value) {
    return (FSize) sin(value);
}

FSize math_sqrt_f(FSize const value) {
    return (FSize) sqrt(value);
}

FSize math_tan_f(FSize const value) {
    return (FSize) tan(value);
}

FSize math_trunc_f(FSize const value) {
    return (FSize) trunc(value);
}