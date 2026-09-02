/*
 * test_ease.c - Tests for include/math/ease.c (full glmc_ease_* coverage)
 *
 * Every easing function maps t=0 -> 0 and t=1 -> 1 (the standard convention),
 * verified against the cglm inline source (dep/cglm/ease.h). The one exception
 * is bounce_inout: cglm computes 0.5 * (1 - bounce_out(2t)) for t < 0.5, so at
 * t=0 it returns 0.5, not 0. Endpoints are asserted as cglm actually returns
 * them. Linear also gets a 0.5 -> 0.5 midpoint sanity check.
 */

#include <math.h>
#include <stdio.h>

#include <math/ease.h>

#include "check.h"

// === Helpers ===

// tight tol for exact polynomial/trig endpoints; loose tol for float-noisy
// elastic and bounce endpoints (large-argument sinf / accumulated rounding).
#define _FTOL 1e-4
#define _TOL 1e-6

int main(void) {
    printf("=== ease module tests ===\n");

    // --- back (overshoots, but endpoints still land on 0 and 1) ---
    printf("--- back ---\n");
    _check_f("back_in(0)",     math_ease_back_in(0.0),     0.0, _TOL);
    _check_f("back_in(1)",     math_ease_back_in(1.0),     1.0, _TOL);
    _check_f("back_inout(0)",  math_ease_back_inout(0.0),  0.0, _TOL);
    _check_f("back_inout(1)",  math_ease_back_inout(1.0),  1.0, _TOL);
    _check_f("back_out(0)",    math_ease_back_out(0.0),    0.0, _TOL);
    _check_f("back_out(1)",    math_ease_back_out(1.0),    1.0, _TOL);

    // --- bounce (bounce_inout(0) == 0.5 per cglm, not 0) ---
    printf("--- bounce ---\n");
    _check_f("bounce_in(0)",     math_ease_bounce_in(0.0),     0.0, _FTOL);
    _check_f("bounce_in(1)",     math_ease_bounce_in(1.0),     1.0, _FTOL);
    _check_f("bounce_inout(0)",  math_ease_bounce_inout(0.0),  0.5, _FTOL);
    _check_f("bounce_inout(1)",  math_ease_bounce_inout(1.0),  1.0, _FTOL);
    _check_f("bounce_out(0)",    math_ease_bounce_out(0.0),    0.0, _FTOL);
    _check_f("bounce_out(1)",    math_ease_bounce_out(1.0),    1.0, _FTOL);

    // --- circ ---
    printf("--- circ ---\n");
    _check_f("circ_in(0)",     math_ease_circ_in(0.0),     0.0, _TOL);
    _check_f("circ_in(1)",     math_ease_circ_in(1.0),     1.0, _TOL);
    _check_f("circ_inout(0)",  math_ease_circ_inout(0.0),  0.0, _TOL);
    _check_f("circ_inout(1)",  math_ease_circ_inout(1.0),  1.0, _TOL);
    _check_f("circ_out(0)",    math_ease_circ_out(0.0),    0.0, _TOL);
    _check_f("circ_out(1)",    math_ease_circ_out(1.0),    1.0, _TOL);

    // --- cubic ---
    printf("--- cubic ---\n");
    _check_f("cubic_in(0)",     math_ease_cubic_in(0.0),     0.0, _TOL);
    _check_f("cubic_in(1)",     math_ease_cubic_in(1.0),     1.0, _TOL);
    _check_f("cubic_inout(0)",  math_ease_cubic_inout(0.0),  0.0, _TOL);
    _check_f("cubic_inout(1)",  math_ease_cubic_inout(1.0),  1.0, _TOL);
    _check_f("cubic_out(0)",    math_ease_cubic_out(0.0),    0.0, _TOL);
    _check_f("cubic_out(1)",    math_ease_cubic_out(1.0),    1.0, _TOL);

    // --- elast (oscillates; endpoints land on 0 and 1) ---
    printf("--- elast ---\n");
    _check_f("elast_in(0)",     math_ease_elast_in(0.0),     0.0, _FTOL);
    _check_f("elast_in(1)",     math_ease_elast_in(1.0),     1.0, _FTOL);
    _check_f("elast_inout(0)",  math_ease_elast_inout(0.0),  0.0, _FTOL);
    _check_f("elast_inout(1)",  math_ease_elast_inout(1.0),  1.0, _FTOL);
    _check_f("elast_out(0)",    math_ease_elast_out(0.0),    0.0, _FTOL);
    _check_f("elast_out(1)",    math_ease_elast_out(1.0),    1.0, _FTOL);

    // --- exp ---
    printf("--- exp ---\n");
    _check_f("exp_in(0)",     math_ease_exp_in(0.0),     0.0, _TOL);
    _check_f("exp_in(1)",     math_ease_exp_in(1.0),     1.0, _TOL);
    _check_f("exp_inout(0)",  math_ease_exp_inout(0.0),  0.0, _TOL);
    _check_f("exp_inout(1)",  math_ease_exp_inout(1.0),  1.0, _TOL);
    _check_f("exp_out(0)",    math_ease_exp_out(0.0),    0.0, _TOL);
    _check_f("exp_out(1)",    math_ease_exp_out(1.0),    1.0, _TOL);

    // --- linear (identity) + midpoint sanity ---
    printf("--- linear ---\n");
    _check_f("linear(0)",    math_ease_linear(0.0),    0.0, _TOL);
    _check_f("linear(0.5)",  math_ease_linear(0.5),    0.5, _TOL);
    _check_f("linear(1)",    math_ease_linear(1.0),    1.0, _TOL);

    // --- quad ---
    printf("--- quad ---\n");
    _check_f("quad_in(0)",     math_ease_quad_in(0.0),     0.0, _TOL);
    _check_f("quad_in(1)",     math_ease_quad_in(1.0),     1.0, _TOL);
    _check_f("quad_inout(0)",  math_ease_quad_inout(0.0),  0.0, _TOL);
    _check_f("quad_inout(1)",  math_ease_quad_inout(1.0),  1.0, _TOL);
    _check_f("quad_out(0)",    math_ease_quad_out(0.0),    0.0, _TOL);
    _check_f("quad_out(1)",    math_ease_quad_out(1.0),    1.0, _TOL);

    // --- quart ---
    printf("--- quart ---\n");
    _check_f("quart_in(0)",     math_ease_quart_in(0.0),     0.0, _TOL);
    _check_f("quart_in(1)",     math_ease_quart_in(1.0),     1.0, _TOL);
    _check_f("quart_inout(0)",  math_ease_quart_inout(0.0),  0.0, _TOL);
    _check_f("quart_inout(1)",  math_ease_quart_inout(1.0),  1.0, _TOL);
    _check_f("quart_out(0)",    math_ease_quart_out(0.0),    0.0, _TOL);
    _check_f("quart_out(1)",    math_ease_quart_out(1.0),    1.0, _TOL);

    // --- quint ---
    printf("--- quint ---\n");
    _check_f("quint_in(0)",     math_ease_quint_in(0.0),     0.0, _TOL);
    _check_f("quint_in(1)",     math_ease_quint_in(1.0),     1.0, _TOL);
    _check_f("quint_inout(0)",  math_ease_quint_inout(0.0),  0.0, _TOL);
    _check_f("quint_inout(1)",  math_ease_quint_inout(1.0),  1.0, _TOL);
    _check_f("quint_out(0)",    math_ease_quint_out(0.0),    0.0, _TOL);
    _check_f("quint_out(1)",    math_ease_quint_out(1.0),    1.0, _TOL);

    // --- sine ---
    printf("--- sine ---\n");
    _check_f("sine_in(0)",     math_ease_sine_in(0.0),     0.0, _TOL);
    _check_f("sine_in(1)",     math_ease_sine_in(1.0),     1.0, _TOL);
    _check_f("sine_inout(0)",  math_ease_sine_inout(0.0),  0.0, _TOL);
    _check_f("sine_inout(1)",  math_ease_sine_inout(1.0),  1.0, _TOL);
    _check_f("sine_out(0)",    math_ease_sine_out(0.0),    0.0, _TOL);
    _check_f("sine_out(1)",    math_ease_sine_out(1.0),    1.0, _TOL);

    return _check_finish();
}