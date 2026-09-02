/*
 * ease.c - Scalar easing-function wrappers for the CFW math module.
 *
 * See ease.h for API documentation and usage examples.
 */

#include <math/ease.h>

/*==============================================================================
 * MARK: - Ease API
 *============================================================================*/

FSize math_ease_back_in(FSize const t) {
    return (FSize) glmc_ease_back_in((float) t);
}

FSize math_ease_back_inout(FSize const t) {
    return (FSize) glmc_ease_back_inout((float) t);
}

FSize math_ease_back_out(FSize const t) {
    return (FSize) glmc_ease_back_out((float) t);
}

FSize math_ease_bounce_in(FSize const t) {
    return (FSize) glmc_ease_bounce_in((float) t);
}

FSize math_ease_bounce_inout(FSize const t) {
    return (FSize) glmc_ease_bounce_inout((float) t);
}

FSize math_ease_bounce_out(FSize const t) {
    return (FSize) glmc_ease_bounce_out((float) t);
}

FSize math_ease_circ_in(FSize const t) {
    return (FSize) glmc_ease_circ_in((float) t);
}

FSize math_ease_circ_inout(FSize const t) {
    return (FSize) glmc_ease_circ_inout((float) t);
}

FSize math_ease_circ_out(FSize const t) {
    return (FSize) glmc_ease_circ_out((float) t);
}

FSize math_ease_cubic_in(FSize const t) {
    return (FSize) glmc_ease_cubic_in((float) t);
}

FSize math_ease_cubic_inout(FSize const t) {
    return (FSize) glmc_ease_cubic_inout((float) t);
}

FSize math_ease_cubic_out(FSize const t) {
    return (FSize) glmc_ease_cubic_out((float) t);
}

FSize math_ease_elast_in(FSize const t) {
    return (FSize) glmc_ease_elast_in((float) t);
}

FSize math_ease_elast_inout(FSize const t) {
    return (FSize) glmc_ease_elast_inout((float) t);
}

FSize math_ease_elast_out(FSize const t) {
    return (FSize) glmc_ease_elast_out((float) t);
}

FSize math_ease_exp_in(FSize const t) {
    return (FSize) glmc_ease_exp_in((float) t);
}

FSize math_ease_exp_inout(FSize const t) {
    return (FSize) glmc_ease_exp_inout((float) t);
}

FSize math_ease_exp_out(FSize const t) {
    return (FSize) glmc_ease_exp_out((float) t);
}

FSize math_ease_linear(FSize const t) {
    return (FSize) glmc_ease_linear((float) t);
}

FSize math_ease_quad_in(FSize const t) {
    return (FSize) glmc_ease_quad_in((float) t);
}

FSize math_ease_quad_inout(FSize const t) {
    return (FSize) glmc_ease_quad_inout((float) t);
}

FSize math_ease_quad_out(FSize const t) {
    return (FSize) glmc_ease_quad_out((float) t);
}

FSize math_ease_quart_in(FSize const t) {
    return (FSize) glmc_ease_quart_in((float) t);
}

FSize math_ease_quart_inout(FSize const t) {
    return (FSize) glmc_ease_quart_inout((float) t);
}

FSize math_ease_quart_out(FSize const t) {
    return (FSize) glmc_ease_quart_out((float) t);
}

FSize math_ease_quint_in(FSize const t) {
    return (FSize) glmc_ease_quint_in((float) t);
}

FSize math_ease_quint_inout(FSize const t) {
    return (FSize) glmc_ease_quint_inout((float) t);
}

FSize math_ease_quint_out(FSize const t) {
    return (FSize) glmc_ease_quint_out((float) t);
}

FSize math_ease_sine_in(FSize const t) {
    return (FSize) glmc_ease_sine_in((float) t);
}

FSize math_ease_sine_inout(FSize const t) {
    return (FSize) glmc_ease_sine_inout((float) t);
}

FSize math_ease_sine_out(FSize const t) {
    return (FSize) glmc_ease_sine_out((float) t);
}